/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/GstAudioSender.h"

#include "audio/AudioTypes.h"
#include "audio/GstAudioPipeline.h"
#include "base/Log.h"

#if defined(__APPLE__)
#include "audio/MacAudioCapture.h"
#endif

#include <gst/gst.h>

GstAudioSender::GstAudioSender(std::string serverHost, uint16_t rtpPort)
    : m_serverHost(std::move(serverHost)),
      m_rtpPort(rtpPort)
{
}

GstAudioSender::~GstAudioSender()
{
  stop();
}

std::string GstAudioSender::buildLaunch(const char *sourceDescription) const
{
  // Shared encode + packetise + send tail. A small leaky queue decouples the
  // live capture source from the encoder so a momentary stall never blocks
  // capture. opusenc inband-fec lets the decoder conceal lost packets.
  return std::string(sourceDescription) + " ! queue max-size-time=" + std::to_string(kAudioCaptureQueueMs * 1000000LL) +
         " leaky=downstream"
         " ! audioconvert ! audioresample"
         " ! audio/x-raw,rate=" +
         std::to_string(kAudioSampleRate) + ",channels=" + std::to_string(kAudioChannels) +
         " ! opusenc bitrate=" + std::to_string(kAudioBitrate) +
         " inband-fec=true frame-size=" + std::to_string(kAudioOpusFrameMs) +
         " ! rtpopuspay pt=" + std::to_string(kAudioRtpPayloadType) + " ! udpsink host=" + m_serverHost +
         " port=" + std::to_string(m_rtpPort) + " sync=false async=false";
}

bool GstAudioSender::start()
{
  m_pipeline = std::make_unique<GstAudioPipeline>("audio-sender");

#if defined(_WIN32)
  // wasapi2src (GStreamer >= 1.18) captures the default render device's loopback.
  // Fall back to the older wasapisrc if the wasapi2 plugin is unavailable.
  if (!m_pipeline->build(buildLaunch("wasapi2src loopback=true low-latency=true"))) {
    LOG_WARN("audio sender: wasapi2src unavailable, falling back to wasapisrc");
    if (!m_pipeline->build(buildLaunch("wasapisrc loopback=true low-latency=true"))) {
      LOG_ERR("audio sender: no usable WASAPI loopback source");
      m_pipeline.reset();
      return false;
    }
  }
#elif defined(__APPLE__)
  // No stock GStreamer system-output loopback source on macOS: feed an appsrc
  // from a trimmed ScreenCaptureKit capture.
  const std::string caps = "audio/x-raw,format=F32LE,rate=" + std::to_string(kAudioSampleRate) +
                           ",channels=" + std::to_string(kAudioChannels) + ",layout=interleaved";
  // do-timestamp=false: MacAudioCapture assigns continuous, sample-counted PTS
  // itself. Wall-clock arrival stamping (do-timestamp=true) turned ScreenCaptureKit's
  // bursty delegate delivery into RTP timestamp discontinuities, which made the
  // receiver's sink hold extra slack and drove up end-to-end latency.
  const std::string src = "appsrc name=macsrc is-live=true format=time do-timestamp=false caps=" + caps;
  if (!m_pipeline->build(buildLaunch(src.c_str()))) {
    LOG_ERR("audio sender: failed to build appsrc capture pipeline");
    m_pipeline.reset();
    return false;
  }
  // Hand the appsrc to the ScreenCaptureKit shim, which pushes buffers into it.
  GstElement *appsrc = m_pipeline->elementByName("macsrc");
  m_macCapture = std::make_unique<MacAudioCapture>();
  const bool macStarted = appsrc != nullptr && m_macCapture->start(appsrc);
  if (appsrc != nullptr) {
    gst_object_unref(appsrc);
  }
  if (!macStarted) {
    LOG_ERR("audio sender: failed to start ScreenCaptureKit capture");
    m_macCapture.reset();
    m_pipeline.reset();
    return false;
  }
#else
  // Linux: capture the monitor of the default sink via PulseAudio/PipeWire.
  if (!m_pipeline->build(buildLaunch("pulsesrc device=@DEFAULT_MONITOR@"))) {
    LOG_WARN("audio sender: monitor source unavailable, falling back to default pulsesrc");
    if (!m_pipeline->build(buildLaunch("pulsesrc"))) {
      LOG_ERR("audio sender: no usable PulseAudio source");
      m_pipeline.reset();
      return false;
    }
  }
#endif

  if (!m_pipeline->start()) {
    LOG_ERR("audio sender: pipeline failed to start");
    stop();
    return false;
  }

  LOG_INFO("audio sender: streaming to %s:%u (RTP/Opus)", m_serverHost.c_str(), m_rtpPort);
  return true;
}

void GstAudioSender::stop()
{
#if defined(__APPLE__)
  if (m_macCapture) {
    m_macCapture->stop();
    m_macCapture.reset();
  }
#endif
  if (m_pipeline) {
    m_pipeline->stop();
    m_pipeline.reset();
  }
}
