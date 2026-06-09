/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/GstAudioReceiver.h"

#include "audio/AudioTypes.h"
#include "audio/GstAudioPipeline.h"
#include "base/Log.h"

#include <gst/gst.h>

GstAudioReceiver::GstAudioReceiver(uint16_t rtpPort) : m_rtpPort(rtpPort)
{
}

GstAudioReceiver::~GstAudioReceiver()
{
  stop();
}

std::string GstAudioReceiver::sinkDescription() const
{
  // No explicit device selected -> let autoaudiosink pick the system default.
  if (m_deviceId.empty()) {
    return "autoaudiosink name=sink";
  }

#if defined(_WIN32)
  const char *element = "wasapi2sink";
#elif defined(__APPLE__)
  const char *element = "osxaudiosink";
#else
  const char *element = "pulsesink";
#endif
  return std::string(element) + " name=sink device=\"" + m_deviceId + "\"";
}

std::string GstAudioReceiver::buildLaunch() const
{
  // The level element posts periodic peak/RMS messages used by the GUI meter;
  // the volume element (named "vol") is the live volume/mute control.
  return "udpsrc port=" + std::to_string(m_rtpPort) + " caps=\"" + audioRtpCaps() +
         "\" ! rtpjitterbuffer latency=" + std::to_string(kAudioJitterBufferMs) +
         " do-lost=true"
         " ! rtpopusdepay ! opusdec plc=true use-inband-fec=true"
         " ! audioconvert ! audioresample"
         " ! level interval=100000000 post-messages=true"
         " ! volume name=vol volume=" +
         std::to_string(m_volume) + " mute=" + (m_mute ? "true" : "false") + " ! " + sinkDescription();
}

bool GstAudioReceiver::start()
{
  m_pipeline = std::make_unique<GstAudioPipeline>("audio-receiver");

  if (!m_pipeline->build(buildLaunch())) {
    // If a specific device failed, fall back to the system default so audio still plays.
    if (!m_deviceId.empty()) {
      LOG_WARN("audio receiver: output device '%s' unavailable, falling back to default", m_deviceId.c_str());
      m_deviceId.clear();
      if (!m_pipeline->build(buildLaunch())) {
        LOG_ERR("audio receiver: failed to build playback pipeline");
        m_pipeline.reset();
        return false;
      }
    } else {
      LOG_ERR("audio receiver: failed to build playback pipeline");
      m_pipeline.reset();
      return false;
    }
  }

  if (!m_pipeline->start()) {
    LOG_ERR("audio receiver: pipeline failed to start on RTP port %u", m_rtpPort);
    stop();
    return false;
  }

  m_running = true;
  LOG_INFO("audio receiver: playing RTP/Opus on UDP port %u", m_rtpPort);
  return true;
}

void GstAudioReceiver::stop()
{
  m_running = false;
  if (m_pipeline) {
    m_pipeline->stop();
    m_pipeline.reset();
  }
}

void GstAudioReceiver::setVolume(double volume)
{
  m_volume = volume;
  if (m_pipeline) {
    m_pipeline->setElementDouble("vol", "volume", volume);
  }
}

void GstAudioReceiver::setMute(bool mute)
{
  m_mute = mute;
  if (m_pipeline) {
    m_pipeline->setElementBool("vol", "mute", mute);
  }
}

void GstAudioReceiver::setOutputDeviceId(const std::string &deviceId)
{
  if (deviceId == m_deviceId) {
    return;
  }
  m_deviceId = deviceId;
  if (m_running) {
    // Sinks generally cannot switch device while PLAYING — rebuild the pipeline.
    LOG_INFO("audio receiver: switching output device, restarting pipeline");
    stop();
    start();
  }
}
