/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "audio/IAudioCapture.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

#ifdef __OBJC__
@class SCStream;
@class DeskflowAudioStreamOutput;
#else
typedef struct objc_object SCStream;
typedef struct objc_object DeskflowAudioStreamOutput;
#endif

/// ScreenCaptureKit-based system audio capture (requires macOS 13+).
/// The user must have granted "Screen & System Audio Recording" permission.
class MacAudioCapture : public IAudioCapture
{
public:
  MacAudioCapture();
  ~MacAudioCapture() override;

  MacAudioCapture(const MacAudioCapture &) = delete;
  MacAudioCapture &operator=(const MacAudioCapture &) = delete;

  bool start() override;
  void stop() override;
  size_t readFrames(float *buf, size_t frames) override;

  /// Called by the Objective-C delegate when audio samples arrive.
  void appendSamples(const float *data, size_t frames);

private:
  SCStream *m_stream = nullptr;
  DeskflowAudioStreamOutput *m_delegate = nullptr;

  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::vector<float> m_ringBuffer;
  size_t m_writePos = 0;
  size_t m_readPos = 0;
  std::atomic<bool> m_running{false};

  static constexpr size_t kRingFrames = 8192;
};
