/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "audio/IAudioCapture.h"

struct pa_simple;

/// PulseAudio loopback capture using the default monitor source.
class LinuxAudioCapture : public IAudioCapture
{
public:
  LinuxAudioCapture() = default;
  ~LinuxAudioCapture() override;

  LinuxAudioCapture(const LinuxAudioCapture &) = delete;
  LinuxAudioCapture &operator=(const LinuxAudioCapture &) = delete;

  bool start() override;
  void stop() override;
  size_t readFrames(float *buf, size_t frames) override;

private:
  pa_simple *m_stream = nullptr;
};
