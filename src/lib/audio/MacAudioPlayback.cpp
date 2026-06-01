/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/MacAudioPlayback.h"

#include "audio/AudioTypes.h"
#include "base/Log.h"

#include <AudioToolbox/AudioToolbox.h>
#include <mutex>
#include <vector>

// Ring buffer shared between writeFrames() and the CoreAudio render callback.
namespace {

struct PlaybackState
{
  std::mutex mutex;
  std::vector<float> ring;
  size_t writePos = 0;
  size_t readPos = 0;

  static constexpr size_t kCapacity = 16384 * 2; // stereo frames

  PlaybackState() : ring(kCapacity, 0.0f)
  {
  }

  void push(const float *buf, size_t samples)
  {
    std::lock_guard<std::mutex> lock(mutex);
    for (size_t i = 0; i < samples; ++i) {
      ring[writePos] = buf[i];
      writePos = (writePos + 1) % kCapacity;
      if (writePos == readPos) {
        readPos = (readPos + 1) % kCapacity; // drop oldest
      }
    }
  }

  size_t pop(float *buf, size_t samples)
  {
    std::lock_guard<std::mutex> lock(mutex);
    size_t copied = 0;
    while (copied < samples && readPos != writePos) {
      buf[copied++] = ring[readPos];
      readPos = (readPos + 1) % kCapacity;
    }
    // Silence any unfilled portion
    for (size_t i = copied; i < samples; ++i) {
      buf[i] = 0.0f;
    }
    return copied;
  }
};

PlaybackState g_state;

OSStatus renderCallback(
    void * /*inRefCon*/, AudioUnitRenderActionFlags * /*ioActionFlags*/, const AudioTimeStamp * /*inTimeStamp*/,
    UInt32 /*inBusNumber*/, UInt32 inNumberFrames, AudioBufferList *ioData
)
{
  for (UInt32 b = 0; b < ioData->mNumberBuffers; ++b) {
    auto *out = static_cast<float *>(ioData->mBuffers[b].mData);
    const UInt32 channels = ioData->mBuffers[b].mNumberChannels;
    g_state.pop(out, inNumberFrames * channels);
  }
  return noErr;
}

} // namespace

MacAudioPlayback::~MacAudioPlayback()
{
  stop();
}

bool MacAudioPlayback::start()
{
  AudioComponentDescription desc = {};
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_DefaultOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;

  AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
  if (comp == nullptr) {
    LOG_ERR("CoreAudio: no default output component found");
    return false;
  }

  if (AudioComponentInstanceNew(comp, &m_outputUnit) != noErr) {
    LOG_ERR("CoreAudio: AudioComponentInstanceNew failed");
    return false;
  }

  AudioStreamBasicDescription fmt = {};
  fmt.mSampleRate = kAudioSampleRate;
  fmt.mFormatID = kAudioFormatLinearPCM;
  fmt.mFormatFlags = static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked) |
                     static_cast<AudioFormatFlags>(kAudioFormatFlagIsNonInterleaved);
  fmt.mBytesPerPacket = sizeof(float);
  fmt.mFramesPerPacket = 1;
  fmt.mBytesPerFrame = sizeof(float);
  fmt.mChannelsPerFrame = static_cast<UInt32>(kAudioChannels);
  fmt.mBitsPerChannel = 32;

  AudioUnitSetProperty(m_outputUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));

  AURenderCallbackStruct cb = {renderCallback, nullptr};
  AudioUnitSetProperty(m_outputUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));

  if (AudioUnitInitialize(m_outputUnit) != noErr) {
    LOG_ERR("CoreAudio: AudioUnitInitialize failed");
    AudioComponentInstanceDispose(m_outputUnit);
    m_outputUnit = nullptr;
    return false;
  }

  if (AudioOutputUnitStart(m_outputUnit) != noErr) {
    LOG_ERR("CoreAudio: AudioOutputUnitStart failed");
    AudioUnitUninitialize(m_outputUnit);
    AudioComponentInstanceDispose(m_outputUnit);
    m_outputUnit = nullptr;
    return false;
  }

  LOG_INFO("CoreAudio playback started");
  return true;
}

void MacAudioPlayback::stop()
{
  if (m_outputUnit != nullptr) {
    AudioOutputUnitStop(m_outputUnit);
    AudioUnitUninitialize(m_outputUnit);
    AudioComponentInstanceDispose(m_outputUnit);
    m_outputUnit = nullptr;
  }
}

void MacAudioPlayback::writeFrames(const float *buf, size_t frames)
{
  g_state.push(buf, frames * static_cast<size_t>(kAudioChannels));
}
