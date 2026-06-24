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
  // Robust fallback: let autoaudiosink pick a working system default. Used when
  // low-latency is off and no device is selected, or after a low-latency sink
  // failed to build (m_forceDefaultSink). autoaudiosink can't take buffer-time.
  if (m_forceDefaultSink || (!m_lowLatency && m_deviceId.empty())) {
    return "autoaudiosink name=sink";
  }

#if defined(_WIN32)
  const char *element = "wasapi2sink";
#elif defined(__APPLE__)
  const char *element = "osxaudiosink";
#else
  const char *element = "pulsesink";
#endif

  std::string sink = std::string(element) + " name=sink";

  // Continuously micro-resample to track the master clock instead of the default
  // "skew" method. With two machines streaming over the network their audio
  // crystals drift slightly; "skew" lets that drift build to ~40 ms and then
  // realigns in one jump (an audible glitch, a latency sawtooth, and the periodic
  // "correct clock skew" warnings). "resample" corrects it smoothly and inaudibly.
  sink += " slave-method=resample";

  if (m_lowLatency) {
    // Replace the sink's ~200 ms default ring buffer with a small one — this is
    // the dominant (and media-dependent) latency knob. WASAPI sinks also expose
    // an explicit low-latency mode that picks the smallest device period.
    sink += " buffer-time=" + std::to_string(kAudioSinkBufferTimeUs) +
            " latency-time=" + std::to_string(kAudioSinkLatencyTimeUs);
#if defined(_WIN32)
    sink += " low-latency=true";
#endif
  }

  // The device is applied after the pipeline is built (setElementDevice), not
  // embedded here: a device id can contain backslashes/quotes that
  // gst_parse_launch would mangle, and is an int on macOS, not a string.
  return sink;
}

std::string GstAudioReceiver::buildLaunch() const
{
  // The level element posts periodic peak/RMS messages used by the GUI meter;
  // the volume element (named "vol") is the live volume/mute control.
  const int jitterMs = m_lowLatency ? kAudioJitterBufferLowMs : kAudioJitterBufferMs;
  return "udpsrc port=" + std::to_string(m_rtpPort) + " caps=\"" + audioRtpCaps() +
         "\" ! rtpjitterbuffer latency=" + std::to_string(jitterMs) +
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
  m_forceDefaultSink = false;

  if (!m_pipeline->build(buildLaunch())) {
    // Only the explicit low-latency sink element fails at parse time -> retry
    // with autoaudiosink. A bad device, by contrast, fails later (at PLAYING).
    if (!m_lowLatency) {
      LOG_ERR("audio receiver: failed to build playback pipeline");
      m_pipeline.reset();
      return false;
    }
    LOG_WARN("audio receiver: low-latency sink unavailable, falling back to default sink");
    m_forceDefaultSink = true;
    if (!m_pipeline->build(buildLaunch())) {
      LOG_ERR("audio receiver: failed to build playback pipeline");
      m_pipeline.reset();
      return false;
    }
  }

  // Apply the selected output device by its real type after the pipeline is
  // built -- never through the gst_parse_launch string.
  if (!m_deviceId.empty()) {
    m_pipeline->setElementDevice("sink", m_deviceId);
  }

  if (!m_pipeline->start()) {
    // A selected device may be unavailable -> drop it and retry once on default.
    if (!m_deviceId.empty()) {
      LOG_WARN("audio receiver: output device '%s' unavailable, falling back to default", m_deviceId.c_str());
      m_deviceId.clear();
      return start();
    }
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

void GstAudioReceiver::setLowLatency(bool lowLatency)
{
  if (lowLatency == m_lowLatency) {
    return;
  }
  m_lowLatency = lowLatency;
  if (m_running) {
    // Sink/buffer sizing is fixed when the pipeline is built — rebuild to apply.
    LOG_INFO("audio receiver: switching low-latency mode %s, restarting pipeline", lowLatency ? "on" : "off");
    stop();
    start();
  }
}
