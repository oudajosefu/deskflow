/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstddef>

/// Platform-agnostic interface for capturing system audio output (loopback).
class IAudioCapture
{
public:
  virtual ~IAudioCapture() = default;

  /// Open the capture device and start recording. Returns false on failure.
  virtual bool start() = 0;

  /// Stop capture and release device resources.
  virtual void stop() = 0;

  /// Read interleaved float32 PCM samples into buf (capacity = frames * channels).
  /// Returns the number of frames actually read (may be less than requested).
  virtual size_t readFrames(float *buf, size_t frames) = 0;
};
