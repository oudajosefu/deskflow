/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/MacAudioCapture.h"

#include "audio/AudioTypes.h"
#include "base/Log.h"

#import <AVFoundation/AVFoundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

// ---------------------------------------------------------------------------
// Objective-C delegate – receives audio buffers from SCStream
// ---------------------------------------------------------------------------

@interface DeskflowAudioStreamOutput : NSObject <SCStreamOutput>
- (instancetype)initWithCapture:(MacAudioCapture *)capture;
@end

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

  CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
  if (blockBuffer == nullptr) {
    return;
  }

  const CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sampleBuffer);
  const AudioStreamBasicDescription *asbd = CMAudioFormatDescriptionGetStreamBasicDescription(fmt);
  if (asbd == nullptr) {
    return;
  }

  size_t length = 0;
  char *dataPtr = nullptr;
  if (CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &length, &dataPtr) != kCMBlockBufferNoErr) {
    return;
  }

  // SCStream delivers float32 interleaved by default when we request it
  const size_t frames = length / (sizeof(float) * static_cast<size_t>(asbd->mChannelsPerFrame));
  _capture->appendSamples(reinterpret_cast<const float *>(dataPtr), frames);
}

@end

// ---------------------------------------------------------------------------
// C++ implementation
// ---------------------------------------------------------------------------

MacAudioCapture::MacAudioCapture() : m_ringBuffer(kRingFrames * static_cast<size_t>(kAudioChannels), 0.0f)
{
}

MacAudioCapture::~MacAudioCapture()
{
  stop();
}

bool MacAudioCapture::start()
{
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
      return false;
    }

    SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
    config.capturesAudio = YES;
    config.excludesCurrentProcessAudio = NO;
    // Request float32 interleaved at our target sample rate
    config.sampleRate = kAudioSampleRate;
    config.channelCount = kAudioChannels;

    // Capture the entire display (we only care about the audio, not video frames)
    SCDisplay *display = content.displays.firstObject;
    if (display == nil) {
      LOG_ERR("no display found for ScreenCaptureKit audio capture");
      return false;
    }

    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];

    m_delegate = [[DeskflowAudioStreamOutput alloc] initWithCapture:this];
    m_stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:nil];

    NSError *addError = nil;
    [m_stream addStreamOutput:m_delegate type:SCStreamOutputTypeAudio sampleHandlerQueue:nil error:&addError];
    if (addError != nil) {
      LOG_ERR("SCStream addStreamOutput failed: %s", addError.localizedDescription.UTF8String);
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
      return false;
    }

    m_running = true;
    LOG_INFO("macOS ScreenCaptureKit audio capture started");
    return true;
  } else {
    LOG_ERR("audio capture requires macOS 13.0 or later");
    return false;
  }
}

void MacAudioCapture::stop()
{
  if (m_stream != nullptr) {
    m_running = false;
    [m_stream stopCaptureWithCompletionHandler:^(NSError *) {
    }];
    m_stream = nullptr;
    m_delegate = nullptr;
    m_cv.notify_all();
  }
}

size_t MacAudioCapture::readFrames(float *buf, size_t frames)
{
  std::unique_lock<std::mutex> lock(m_mutex);
  const size_t want = frames * static_cast<size_t>(kAudioChannels);
  // Wait up to 100 ms for enough data
  m_cv.wait_for(lock, std::chrono::milliseconds(100), [&] {
    const size_t avail =
        (m_writePos >= m_readPos) ? (m_writePos - m_readPos) : (m_ringBuffer.size() - m_readPos + m_writePos);
    return avail >= want || !m_running;
  });

  size_t copied = 0;
  while (copied < want) {
    if (m_readPos == m_writePos) {
      break;
    }
    buf[copied++] = m_ringBuffer[m_readPos];
    m_readPos = (m_readPos + 1) % m_ringBuffer.size();
  }
  return copied / static_cast<size_t>(kAudioChannels);
}

void MacAudioCapture::appendSamples(const float *data, size_t frames)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  const size_t samples = frames * static_cast<size_t>(kAudioChannels);
  for (size_t i = 0; i < samples; ++i) {
    m_ringBuffer[m_writePos] = data[i];
    m_writePos = (m_writePos + 1) % m_ringBuffer.size();
    // Overwrite oldest data if ring is full
    if (m_writePos == m_readPos) {
      m_readPos = (m_readPos + 1) % m_ringBuffer.size();
    }
  }
  m_cv.notify_one();
}
