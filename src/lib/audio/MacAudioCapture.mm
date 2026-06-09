/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/MacAudioCapture.h"

#include "audio/AudioTypes.h"
#include "base/Log.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#import <AVFoundation/AVFoundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

// ---------------------------------------------------------------------------
// Objective-C delegate – receives audio buffers from SCStream
// ---------------------------------------------------------------------------

API_AVAILABLE(macos(13.0))
@interface DeskflowAudioStreamOutput : NSObject <SCStreamOutput>
- (instancetype)initWithCapture:(MacAudioCapture *)capture;
@end

API_AVAILABLE(macos(13.0))
@implementation DeskflowAudioStreamOutput {
  MacAudioCapture *_capture;
}

- (instancetype)initWithCapture:(MacAudioCapture *)capture
{
  self = [super init];
  if (self) {
    _capture = capture;
  }
  return self;
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
  if (type != SCStreamOutputTypeAudio) {
    return;
  }

  const CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sampleBuffer);
  const AudioStreamBasicDescription *asbd = fmt ? CMAudioFormatDescriptionGetStreamBasicDescription(fmt) : nullptr;
  if (asbd == nullptr || asbd->mChannelsPerFrame == 0) {
    return;
  }

  const CMItemCount frames = CMSampleBufferGetNumSamples(sampleBuffer);
  if (frames <= 0) {
    return;
  }

  // ScreenCaptureKit delivers audio as planar (non-interleaved) float32 by default, so a single CMBlockBuffer data
  // pointer would expose only one channel. Use the AudioBufferList API and interleave to stereo, which is the layout
  // the appsrc caps (F32LE interleaved) advertise.
  const UInt32 channels = asbd->mChannelsPerFrame;
  const size_t ablSize = sizeof(AudioBufferList) + (channels > 1 ? (channels - 1) * sizeof(AudioBuffer) : 0);
  std::vector<uint8_t> ablStorage(ablSize, 0);
  auto *abl = reinterpret_cast<AudioBufferList *>(ablStorage.data());

  CMBlockBufferRef blockBuffer = nullptr;
  const OSStatus status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
      sampleBuffer, nullptr, abl, ablSize, nullptr, nullptr, 0, &blockBuffer
  );
  if (status != noErr || blockBuffer == nullptr) {
    return;
  }

  std::vector<float> interleaved(static_cast<size_t>(frames) * static_cast<size_t>(kAudioChannels), 0.0f);

  if (abl->mNumberBuffers == 1) {
    // Interleaved source: one buffer holding all channels.
    const auto *src = reinterpret_cast<const float *>(abl->mBuffers[0].mData);
    const UInt32 srcChannels = abl->mBuffers[0].mNumberChannels > 0 ? abl->mBuffers[0].mNumberChannels : channels;
    if (src != nullptr) {
      for (CMItemCount f = 0; f < frames; ++f) {
        const float left = src[static_cast<size_t>(f) * srcChannels];
        const float right = (srcChannels >= 2) ? src[static_cast<size_t>(f) * srcChannels + 1] : left;
        interleaved[static_cast<size_t>(f) * kAudioChannels] = left;
        interleaved[static_cast<size_t>(f) * kAudioChannels + 1] = right;
      }
    }
  } else {
    // Planar source: one buffer per channel.
    const auto *leftPlane = reinterpret_cast<const float *>(abl->mBuffers[0].mData);
    const auto *rightPlane =
        (abl->mNumberBuffers >= 2) ? reinterpret_cast<const float *>(abl->mBuffers[1].mData) : leftPlane;
    if (leftPlane != nullptr && rightPlane != nullptr) {
      for (CMItemCount f = 0; f < frames; ++f) {
        interleaved[static_cast<size_t>(f) * kAudioChannels] = leftPlane[f];
        interleaved[static_cast<size_t>(f) * kAudioChannels + 1] = rightPlane[f];
      }
    }
  }

  _capture->pushSamples(interleaved.data(), static_cast<size_t>(frames));

  CFRelease(blockBuffer);
}

@end

// ---------------------------------------------------------------------------
// C++ implementation
// ---------------------------------------------------------------------------

MacAudioCapture::MacAudioCapture() = default;

MacAudioCapture::~MacAudioCapture()
{
  stop();
}

bool MacAudioCapture::start(GstElement *appsrc)
{
  if (appsrc == nullptr) {
    return false;
  }
  m_appsrc = GST_ELEMENT(gst_object_ref(appsrc));

  if (@available(macOS 13.0, *)) {
    // Request shareable content synchronously via a semaphore
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block SCShareableContent *content = nil;
    __block NSError *contentError = nil;

    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *c, NSError *e) {
      content = c;
      contentError = e;
      dispatch_semaphore_signal(sem);
    }];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

    if (contentError != nil || content == nil) {
      LOG_ERR("SCShareableContent failed: %s", contentError.localizedDescription.UTF8String ?: "unknown");
      stop();
      return false;
    }

    SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
    config.capturesAudio = YES;
    config.excludesCurrentProcessAudio = NO;
    config.sampleRate = kAudioSampleRate;
    config.channelCount = kAudioChannels;

    SCDisplay *display = content.displays.firstObject;
    if (display == nil) {
      LOG_ERR("no display found for ScreenCaptureKit audio capture");
      stop();
      return false;
    }

    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];

    m_delegate = [[DeskflowAudioStreamOutput alloc] initWithCapture:this];
    m_stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:nil];

    NSError *addError = nil;
    [m_stream addStreamOutput:m_delegate type:SCStreamOutputTypeAudio sampleHandlerQueue:nil error:&addError];
    if (addError != nil) {
      LOG_ERR("SCStream addStreamOutput failed: %s", addError.localizedDescription.UTF8String);
      stop();
      return false;
    }

    __block bool started = false;
    __block NSError *startError = nil;
    dispatch_semaphore_t startSem = dispatch_semaphore_create(0);
    [m_stream startCaptureWithCompletionHandler:^(NSError *e) {
      startError = e;
      started = (e == nil);
      dispatch_semaphore_signal(startSem);
    }];
    dispatch_semaphore_wait(startSem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

    if (!started) {
      LOG_ERR("SCStream startCapture failed: %s", startError ? startError.localizedDescription.UTF8String : "timeout");
      stop();
      return false;
    }

    m_running = true;
    LOG_INFO("macOS ScreenCaptureKit audio capture started (-> appsrc)");
    return true;
  } else {
    LOG_ERR("audio capture requires macOS 13.0 or later");
    stop();
    return false;
  }
}

void MacAudioCapture::stop()
{
  m_running = false;
  if (m_stream != nullptr) {
    [m_stream stopCaptureWithCompletionHandler:^(NSError *) {
    }];
    m_stream = nullptr;
    m_delegate = nullptr;
  }
  if (m_appsrc != nullptr) {
    gst_object_unref(m_appsrc);
    m_appsrc = nullptr;
  }
}

void MacAudioCapture::pushSamples(const float *data, size_t frames)
{
  if (!m_running || m_appsrc == nullptr || frames == 0) {
    return;
  }

  const size_t bytes = frames * static_cast<size_t>(kAudioChannels) * sizeof(float);
  GstBuffer *buffer = gst_buffer_new_allocate(nullptr, bytes, nullptr);
  GstMapInfo map;
  if (gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
    std::memcpy(map.data, data, bytes);
    gst_buffer_unmap(buffer, &map);
  }

  // do-timestamp=true on the appsrc applies running-time PTS for us.
  const GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_FLUSHING) {
    LOG_DEBUG("appsrc push returned %d", static_cast<int>(ret));
  }
}
