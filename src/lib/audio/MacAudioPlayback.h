/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "audio/IAudioPlayback.h"

#include <AudioUnit/AudioUnit.h>

/// CoreAudio playback via the default output AudioUnit.
class MacAudioPlayback : public IAudioPlayback
{
public:
  MacAudioPlayback() = default;
  ~MacAudioPlayback() override;

  MacAudioPlayback(const MacAudioPlayback &) = delete;
  MacAudioPlayback &operator=(const MacAudioPlayback &) = delete;

  bool start() override;
  void stop() override;
  void writeFrames(const float *buf, size_t frames) override;

private:
  AudioUnit m_outputUnit = nullptr;
};
