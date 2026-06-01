/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "audio/IAudioPlayback.h"

#include <Audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

/// WASAPI playback to the default render endpoint.
class WinAudioPlayback : public IAudioPlayback
{
public:
  WinAudioPlayback() = default;
  ~WinAudioPlayback() override;

  WinAudioPlayback(const WinAudioPlayback &) = delete;
  WinAudioPlayback &operator=(const WinAudioPlayback &) = delete;

  bool start() override;
  void stop() override;
  void writeFrames(const float *buf, size_t frames) override;

private:
  Microsoft::WRL::ComPtr<IMMDevice> m_device;
  Microsoft::WRL::ComPtr<IAudioClient> m_audioClient;
  Microsoft::WRL::ComPtr<IAudioRenderClient> m_renderClient;

  WAVEFORMATEX *m_mixFormat = nullptr;
  UINT32 m_bufferFrameCount = 0;
  bool m_comInitialized = false;
};
