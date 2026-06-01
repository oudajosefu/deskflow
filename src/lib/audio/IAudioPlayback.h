/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstddef>

/// Platform-agnostic interface for playing PCM audio through the default output device.
class IAudioPlayback
{
public:
  virtual ~IAudioPlayback() = default;

  /// Open the playback device and prepare it for output. Returns false on failure.
  virtual bool start() = 0;

  /// Stop playback and release device resources.
  virtual void stop() = 0;

  /// Write interleaved float32 PCM samples from buf (frames * channels samples total).
  virtual void writeFrames(const float *buf, size_t frames) = 0;
};
