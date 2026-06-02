/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/WinAudioCapture.h"

#include "audio/AudioTypes.h"
#include "base/Log.h"

#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
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

// True when the device mix format stores IEEE float samples (vs integer PCM).
inline bool formatIsFloat(const WAVEFORMATEX *fmt)
{
  if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    return true;
  }
  if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(fmt);
    return ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  }
  return false;
}

// Convert one channel's sample at p to float in [-1, 1] for the given bit depth.
inline float sampleToFloat(const BYTE *p, WORD bitsPerSample, bool isFloat)
{
  if (isFloat) {
    if (bitsPerSample == 32) {
      float f = 0.0f;
      std::memcpy(&f, p, sizeof(f));
      return f;
    }
    if (bitsPerSample == 64) {
      double d = 0.0;
      std::memcpy(&d, p, sizeof(d));
      return static_cast<float>(d);
    }
    return 0.0f;
  }
  switch (bitsPerSample) {
  case 16: {
    int16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return static_cast<float>(v) / 32768.0f;
  }
  case 32: {
    int32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return static_cast<float>(v) / 2147483648.0f;
  }
  case 24: {
    int32_t v = static_cast<int32_t>(p[0]) | (static_cast<int32_t>(p[1]) << 8) | (static_cast<int32_t>(p[2]) << 16);
    if (v & 0x800000) {
      v |= ~0xFFFFFF; // sign-extend negative 24-bit values
    }
    return static_cast<float>(v) / 8388608.0f;
  }
  case 8:
    return (static_cast<float>(p[0]) - 128.0f) / 128.0f; // 8-bit PCM is unsigned
  default:
    return 0.0f;
  }
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
    LOG_WARN(
        "WASAPI capture: device mix format is %u-bit, %u channel(s), %lu Hz; converting to float32 stereo "
        "(sample-rate differences are not resampled)",
        static_cast<unsigned>(m_mixFormat->wBitsPerSample), static_cast<unsigned>(m_mixFormat->nChannels),
        static_cast<unsigned long>(m_mixFormat->nSamplesPerSec)
    );
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
    float *out = buf + filled * kAudioChannels;
    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
      std::fill(out, out + toCopy * kAudioChannels, 0.0f);
    } else {
      // The WASAPI buffer is in the device mix format (m_mixFormat), which may not be float32 stereo. Convert each
      // frame to interleaved float32 stereo — reading exactly nBlockAlign bytes per frame avoids the buffer overread
      // that a blind float32-stereo memcpy causes on 16/24-bit or mono devices.
      const WORD bits = m_mixFormat->wBitsPerSample;
      const WORD srcChannels = m_mixFormat->nChannels;
      const size_t srcFrameBytes = m_mixFormat->nBlockAlign;
      const size_t bytesPerSample = bits / 8u;
      const bool isFloat = formatIsFloat(m_mixFormat);
      for (size_t f = 0; f < toCopy; ++f) {
        const BYTE *frame = data + f * srcFrameBytes;
        const float left = sampleToFloat(frame, bits, isFloat);
        const float right = (srcChannels >= 2) ? sampleToFloat(frame + bytesPerSample, bits, isFloat) : left;
        out[f * kAudioChannels] = left;
        out[f * kAudioChannels + 1] = right;
      }
    }
    filled += toCopy;
    m_captureClient->ReleaseBuffer(numFrames);
  }
  return filled;
}
