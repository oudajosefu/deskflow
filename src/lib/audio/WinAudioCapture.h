/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "audio/IAudioCapture.h"

#include <Audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

/// WASAPI loopback capture of the default render endpoint.
class WinAudioCapture : public IAudioCapture
{
public:
  WinAudioCapture() = default;
  ~WinAudioCapture() override;

  WinAudioCapture(const WinAudioCapture &) = delete;
  WinAudioCapture &operator=(const WinAudioCapture &) = delete;

  bool start() override;
  void stop() override;
  size_t readFrames(float *buf, size_t frames) override;

private:
  Microsoft::WRL::ComPtr<IMMDevice> m_device;
  Microsoft::WRL::ComPtr<IAudioClient> m_audioClient;
  Microsoft::WRL::ComPtr<IAudioCaptureClient> m_captureClient;

  WAVEFORMATEX *m_mixFormat = nullptr;
  bool m_comInitialized = false;
};
