/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "audio/IAudioCapture.h"

#include <Audioclient.h>
#include <mmdeviceapi.h>
#include <vector>
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
  // When true, the loopback stream uses the device mix format and readFrames() must convert each frame to float32
  // stereo. When false, WASAPI auto-converts to our 48 kHz float32 stereo format and readFrames() copies directly.
  bool m_convertFromDeviceFormat = false;
  // Interleaved float32 stereo samples captured from WASAPI but not yet returned. WASAPI loopback delivers ~10 ms
  // (480-frame) packets while callers ask for 20 ms (960-frame) blocks, so leftover samples must be retained across
  // readFrames() calls instead of discarded; otherwise no full block is ever assembled and nothing is transmitted.
  std::vector<float> m_residual;
};
