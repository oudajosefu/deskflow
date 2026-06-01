/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/WinAudioCapture.h"

#include "audio/AudioTypes.h"
#include "base/Log.h"

#include <functiondiscoverykeys_devpkey.h>
#include <vector>

namespace {

// Convert a WASAPI float32 packet to our target sample rate / channel count if needed.
// For simplicity we assume the mix format is already float32 stereo at 48 kHz
// (the common case on Windows). A production implementation would resample here.
inline bool formatIsCompatible(const WAVEFORMATEX *fmt)
{
  if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    return fmt->nSamplesPerSec == kAudioSampleRate && fmt->nChannels == kAudioChannels;
  }
  if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(fmt);
    return ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && fmt->nSamplesPerSec == kAudioSampleRate &&
           fmt->nChannels == kAudioChannels;
  }
  return false;
}

} // namespace

WinAudioCapture::~WinAudioCapture()
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

bool WinAudioCapture::start()
{
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (SUCCEEDED(hr)) {
    m_comInitialized = true;
  } else if (hr != RPC_E_CHANGED_MODE) {
    LOG_ERR("WASAPI capture: CoInitializeEx failed 0x%08x", hr);
    return false;
  }

  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
  hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
      reinterpret_cast<void **>(enumerator.GetAddressOf())
  );
  if (FAILED(hr)) {
    LOG_ERR("WASAPI capture: cannot create device enumerator 0x%08x", hr);
    return false;
  }

  hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, m_device.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERR("WASAPI capture: cannot get default render endpoint 0x%08x", hr);
    return false;
  }

  hr = m_device->Activate(
      __uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(m_audioClient.GetAddressOf())
  );
  if (FAILED(hr)) {
    LOG_ERR("WASAPI capture: IAudioClient activate failed 0x%08x", hr);
    return false;
  }

  hr = m_audioClient->GetMixFormat(&m_mixFormat);
  if (FAILED(hr)) {
    LOG_ERR("WASAPI capture: GetMixFormat failed 0x%08x", hr);
    return false;
  }

  if (!formatIsCompatible(m_mixFormat)) {
    LOG_WARN("WASAPI capture: mix format is not float32 stereo 48 kHz — audio may be silent");
  }

  constexpr REFERENCE_TIME bufferDuration = 200 * 10000; // 200 ms in 100-ns units
  hr = m_audioClient->Initialize(
      AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, bufferDuration, 0, m_mixFormat, nullptr
  );
  if (FAILED(hr)) {
    LOG_ERR("WASAPI capture: Initialize failed 0x%08x", hr);
    return false;
  }

  hr = m_audioClient->GetService(
      __uuidof(IAudioCaptureClient), reinterpret_cast<void **>(m_captureClient.GetAddressOf())
  );
  if (FAILED(hr)) {
    LOG_ERR("WASAPI capture: GetService(IAudioCaptureClient) failed 0x%08x", hr);
    return false;
  }

  hr = m_audioClient->Start();
  if (FAILED(hr)) {
    LOG_ERR("WASAPI capture: Start failed 0x%08x", hr);
    return false;
  }

  LOG_INFO("WASAPI loopback capture started");
  return true;
}

void WinAudioCapture::stop()
{
  if (m_audioClient != nullptr) {
    m_audioClient->Stop();
  }
  m_captureClient = nullptr;
  m_audioClient = nullptr;
  m_device = nullptr;
}

size_t WinAudioCapture::readFrames(float *buf, size_t frames)
{
  if (m_captureClient == nullptr) {
    return 0;
  }

  size_t filled = 0;
  while (filled < frames) {
    UINT32 packetSize = 0;
    if (FAILED(m_captureClient->GetNextPacketSize(&packetSize)) || packetSize == 0) {
      break;
    }

    BYTE *data = nullptr;
    UINT32 numFrames = 0;
    DWORD flags = 0;
    if (FAILED(m_captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr))) {
      break;
    }

    const size_t toCopy = std::min(static_cast<size_t>(numFrames), frames - filled);
    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
      std::fill(buf + filled * kAudioChannels, buf + (filled + toCopy) * kAudioChannels, 0.0f);
    } else {
      std::memcpy(buf + filled * kAudioChannels, data, toCopy * kAudioChannels * sizeof(float));
    }
    filled += toCopy;
    m_captureClient->ReleaseBuffer(numFrames);
  }
  return filled;
}
