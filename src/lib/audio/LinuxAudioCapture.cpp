/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/LinuxAudioCapture.h"

#include "audio/AudioTypes.h"
#include "base/Log.h"

#include <pulse/error.h>
#include <pulse/simple.h>

LinuxAudioCapture::~LinuxAudioCapture()
{
  stop();
}

bool LinuxAudioCapture::start()
{
  const pa_sample_spec spec = {
      .format = PA_SAMPLE_FLOAT32LE,
      .rate = static_cast<uint32_t>(kAudioSampleRate),
      .channels = static_cast<uint8_t>(kAudioChannels),
  };

  int err = PA_OK;
  // nullptr device = default monitor source (the loopback of the default sink)
  m_stream = pa_simple_new(
      nullptr,    // default PulseAudio server
      "Deskflow", // application name
      PA_STREAM_RECORD,
      nullptr, // default monitor source
      "Audio routing capture", &spec,
      nullptr, // default channel map
      nullptr, // default buffering attributes
      &err
  );

  if (m_stream == nullptr) {
    LOG_ERR("PulseAudio capture open failed: %s", pa_strerror(err));
    return false;
  }

  LOG_INFO("PulseAudio audio capture started");
  return true;
}

void LinuxAudioCapture::stop()
{
  if (m_stream != nullptr) {
    pa_simple_free(m_stream);
    m_stream = nullptr;
  }
}

size_t LinuxAudioCapture::readFrames(float *buf, size_t frames)
{
  if (m_stream == nullptr) {
    return 0;
  }

  const size_t bytes = frames * static_cast<size_t>(kAudioChannels) * sizeof(float);
  int err = PA_OK;
  if (pa_simple_read(m_stream, buf, bytes, &err) < 0) {
    LOG_ERR("PulseAudio read failed: %s", pa_strerror(err));
    return 0;
  }
  return frames;
}
