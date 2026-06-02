/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/WinAudioPlayback.h"

#include "audio/AudioTypes.h"
#include "base/Log.h"

#include <ksmedia.h>
#include <mmreg.h>

#include <cstring>

namespace {

// Build the fixed pipeline format: 48 kHz, stereo, 32-bit IEEE float. Combined with
// AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM this lets the WASAPI shared-mode mixer resample/reformat between this format and
// whatever the device runs at, so playback never assumes the device is already 48 kHz float32 stereo.
WAVEFORMATEXTENSIBLE makeFloat32StereoFormat()
{
  WAVEFORMATEXTENSIBLE wfx = {};
  wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  wfx.Format.nChannels = static_cast<WORD>(kAudioChannels);
  wfx.Format.nSamplesPerSec = static_cast<DWORD>(kAudioSampleRate);
  wfx.Format.wBitsPerSample = 32;
  wfx.Format.nBlockAlign = static_cast<WORD>(wfx.Format.nChannels * (wfx.Format.wBitsPerSample / 8));
  wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * static_cast<DWORD>(wfx.Format.nBlockAlign);
  wfx.Format.cbSize = static_cast<WORD>(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
  wfx.Samples.wValidBitsPerSample = 32;
  wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
  wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  return wfx;
}

} // namespace

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

  // The pipeline produces 48 kHz float32 stereo, but the output device commonly runs at another rate (e.g. 44.1 kHz).
  // Initialize with our fixed format plus AUTOCONVERTPCM so the WASAPI shared-mode mixer resamples/reformats to the
  // device. Without this the 48 kHz samples play out at the device rate (wrong pitch) and the rate mismatch floods the
  // render buffer (dropped samples / crackle). This mirrors how the macOS/Linux backends rely on CoreAudio/PulseAudio.
  LOG_INFO(
      "WASAPI playback: device mix is %lu Hz / %u ch; rendering %d Hz float32 stereo via WASAPI auto-convert",
      static_cast<unsigned long>(m_mixFormat->nSamplesPerSec), static_cast<unsigned>(m_mixFormat->nChannels),
      kAudioSampleRate
  );

  WAVEFORMATEXTENSIBLE wfx = makeFloat32StereoFormat();
  constexpr REFERENCE_TIME bufferDuration = 200 * 10000; // 200 ms
  constexpr DWORD convertFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
  hr = m_audioClient->Initialize(
      AUDCLNT_SHAREMODE_SHARED, convertFlags, bufferDuration, 0, reinterpret_cast<WAVEFORMATEX *>(&wfx), nullptr
  );
  if (FAILED(hr)) {
    // The device rejected AUTOCONVERTPCM for our format. A failed Initialize leaves the IAudioClient in an undefined
    // state, so re-activate it before retrying with the device mix format. writeFrames assumes float32 stereo, so the
    // fallback renders correctly only on natively float32-stereo devices, but it avoids a hard failure.
    LOG_WARN("WASAPI playback: auto-convert Initialize failed 0x%08x, falling back to device mix format", hr);
    m_audioClient.Reset();
    hr = m_device->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(m_audioClient.GetAddressOf())
    );
    if (SUCCEEDED(hr)) {
      hr = m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufferDuration, 0, m_mixFormat, nullptr);
    }
  }
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
