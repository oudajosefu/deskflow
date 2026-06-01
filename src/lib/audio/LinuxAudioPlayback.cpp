/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/LinuxAudioPlayback.h"

#include "audio/AudioTypes.h"
#include "base/Log.h"

#include <pulse/error.h>
#include <pulse/simple.h>

LinuxAudioPlayback::~LinuxAudioPlayback()
{
  stop();
}

bool LinuxAudioPlayback::start()
{
  const pa_sample_spec spec = {
      .format = PA_SAMPLE_FLOAT32LE,
      .rate = static_cast<uint32_t>(kAudioSampleRate),
      .channels = static_cast<uint8_t>(kAudioChannels),
  };

  int err = PA_OK;
  m_stream = pa_simple_new(
      nullptr,    // default PulseAudio server
      "Deskflow", // application name
      PA_STREAM_PLAYBACK,
      nullptr, // default sink
      "Audio routing playback", &spec,
      nullptr, // default channel map
      nullptr, // default buffering attributes
      &err
  );

  if (m_stream == nullptr) {
    LOG_ERR("PulseAudio playback open failed: %s", pa_strerror(err));
    return false;
  }

  LOG_INFO("PulseAudio audio playback started");
  return true;
}

void LinuxAudioPlayback::stop()
{
  if (m_stream != nullptr) {
    int err = PA_OK;
    pa_simple_drain(m_stream, &err);
    pa_simple_free(m_stream);
    m_stream = nullptr;
  }
}

void LinuxAudioPlayback::writeFrames(const float *buf, size_t frames)
{
  if (m_stream == nullptr) {
    return;
  }

  const size_t bytes = frames * static_cast<size_t>(kAudioChannels) * sizeof(float);
  int err = PA_OK;
  if (pa_simple_write(m_stream, buf, bytes, &err) < 0) {
    LOG_ERR("PulseAudio write failed: %s", pa_strerror(err));
  }
}
