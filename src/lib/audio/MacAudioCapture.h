/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <dispatch/dispatch.h>

typedef struct _GstElement GstElement;

#ifdef __OBJC__
@class SCStream;
@class DeskflowAudioStreamOutput;
#else
typedef struct objc_object SCStream;
typedef struct objc_object DeskflowAudioStreamOutput;
#endif

///
/// ScreenCaptureKit-based system audio capture (requires macOS 13+).
///
/// GStreamer has no stock system-output loopback source on macOS, so this thin
/// shim captures with ScreenCaptureKit and pushes the audio into a GStreamer
/// appsrc. From there the normal sender pipeline (opus -> RTP -> udpsink) takes
/// over. The user must have granted "Screen & System Audio Recording" permission.
///
class MacAudioCapture
{
public:
  MacAudioCapture();
  ~MacAudioCapture();

  MacAudioCapture(const MacAudioCapture &) = delete;
  MacAudioCapture &operator=(const MacAudioCapture &) = delete;

  /// Start capture, pushing interleaved F32LE stereo frames into `appsrc`.
  /// Takes its own reference to `appsrc`; returns false on failure.
  bool start(GstElement *appsrc);

  /// Stop capture and release the appsrc reference.
  void stop();

  /// Called by the Objective-C delegate when audio samples arrive
  /// (interleaved stereo float32).
  void pushSamples(const float *data, size_t frames);

private:
  GstElement *m_appsrc = nullptr;
  SCStream *m_stream = nullptr;
  DeskflowAudioStreamOutput *m_delegate = nullptr;
  // Serial queue SCStream delivers sample buffers on; we own it so teardown can
  // drain it before releasing the appsrc the delegate pushes into.
  dispatch_queue_t m_sampleQueue = nullptr;
  std::atomic<bool> m_running{false};

  // Continuous-timeline timestamping for the appsrc (do-timestamp is off): PTS is
  // derived from the running sample count, anchored once to the pipeline clock.
  uint64_t m_framesPushed = 0;
  uint64_t m_basePts = 0; // running-time (ns) of the first captured sample
  bool m_havePtsBase = false;
};
