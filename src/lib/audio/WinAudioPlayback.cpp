/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/WinAudioPlayback.h"

#include "audio/AudioTypes.h"
#include "base/Log.h"

#include <cstring>

WinAudioPlayback::~WinAudioPlayback()
{
  stop();
  if (m_mixFormat != nullptr) {
    CoTaskMemFree(m_mixFormat);
    m_mixFormat = nullptr;
  }
  if (m_comInitialized) {
    CoUninitialize();
  }
}

bool WinAudioPlayback::start()
{
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (SUCCEEDED(hr)) {
    m_comInitialized = true;
  } else if (hr != RPC_E_CHANGED_MODE) {
    LOG_ERR("WASAPI playback: CoInitializeEx failed 0x%08x", hr);
    return false;
  }

  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
  hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
      reinterpret_cast<void **>(enumerator.GetAddressOf())
  );
  if (FAILED(hr)) {
    LOG_ERR("WASAPI playback: cannot create device enumerator 0x%08x", hr);
    return false;
  }

  hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, m_device.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERR("WASAPI playback: cannot get default render endpoint 0x%08x", hr);
    return false;
  }

  hr = m_device->Activate(
      __uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(m_audioClient.GetAddressOf())
  );
  if (FAILED(hr)) {
    LOG_ERR("WASAPI playback: IAudioClient activate failed 0x%08x", hr);
    return false;
  }

  hr = m_audioClient->GetMixFormat(&m_mixFormat);
  if (FAILED(hr)) {
    LOG_ERR("WASAPI playback: GetMixFormat failed 0x%08x", hr);
    return false;
  }

  constexpr REFERENCE_TIME bufferDuration = 200 * 10000; // 200 ms
  hr = m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufferDuration, 0, m_mixFormat, nullptr);
  if (FAILED(hr)) {
    LOG_ERR("WASAPI playback: Initialize failed 0x%08x", hr);
    return false;
  }

  hr = m_audioClient->GetBufferSize(&m_bufferFrameCount);
  if (FAILED(hr)) {
    LOG_ERR("WASAPI playback: GetBufferSize failed 0x%08x", hr);
    return false;
  }

  hr =
      m_audioClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void **>(m_renderClient.GetAddressOf()));
  if (FAILED(hr)) {
    LOG_ERR("WASAPI playback: GetService(IAudioRenderClient) failed 0x%08x", hr);
    return false;
  }

  hr = m_audioClient->Start();
  if (FAILED(hr)) {
    LOG_ERR("WASAPI playback: Start failed 0x%08x", hr);
    return false;
  }

  LOG_INFO("WASAPI audio playback started");
  return true;
}

void WinAudioPlayback::stop()
{
  if (m_audioClient != nullptr) {
    m_audioClient->Stop();
  }
  m_renderClient = nullptr;
  m_audioClient = nullptr;
  m_device = nullptr;
}

void WinAudioPlayback::writeFrames(const float *buf, size_t frames)
{
  if (m_renderClient == nullptr || frames == 0) {
    return;
  }

  UINT32 padding = 0;
  if (FAILED(m_audioClient->GetCurrentPadding(&padding))) {
    return;
  }

  const UINT32 available = m_bufferFrameCount - padding;
  const UINT32 toWrite = static_cast<UINT32>(std::min(static_cast<size_t>(available), frames));
  if (toWrite == 0) {
    return;
  }

  BYTE *data = nullptr;
  if (FAILED(m_renderClient->GetBuffer(toWrite, &data))) {
    return;
  }

  std::memcpy(data, buf, toWrite * static_cast<UINT32>(kAudioChannels) * sizeof(float));
  m_renderClient->ReleaseBuffer(toWrite, 0);
}
