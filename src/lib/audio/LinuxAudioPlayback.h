/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "audio/IAudioPlayback.h"

struct pa_simple;

/// PulseAudio playback to the default output sink.
class LinuxAudioPlayback : public IAudioPlayback
{
public:
  LinuxAudioPlayback() = default;
  ~LinuxAudioPlayback() override;

  LinuxAudioPlayback(const LinuxAudioPlayback &) = delete;
  LinuxAudioPlayback &operator=(const LinuxAudioPlayback &) = delete;

  bool start() override;
  void stop() override;
  void writeFrames(const float *buf, size_t frames) override;

private:
  pa_simple *m_stream = nullptr;
};
