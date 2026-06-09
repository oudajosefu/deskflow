/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

class GstAudioPipeline;
#if defined(__APPLE__)
class MacAudioCapture;
#endif

///
/// Client-side audio sender: captures the local machine's system-output audio,
/// Opus-encodes it and streams it as RTP/UDP to the server.
///
/// The whole capture -> encode -> packetise -> send path is a single GStreamer
/// pipeline:
///
///   <platform-src> ! queue ! audioconvert ! audioresample
///     ! audio/x-raw,rate=48000,channels=2
///     ! opusenc bitrate=96000 inband-fec=true frame-size=20
///     ! rtpopuspay pt=96 ! udpsink host=<server> port=<P> sync=false
///
/// Platform source:
///   * Windows: wasapi2src loopback=true   (falls back to wasapisrc)
///   * Linux:   pulsesrc device=@DEFAULT_MONITOR@
///   * macOS:   appsrc fed by a ScreenCaptureKit shim (no stock loopback element)
///
class GstAudioSender
{
public:
  GstAudioSender(std::string serverHost, uint16_t rtpPort);
  ~GstAudioSender();

  GstAudioSender(const GstAudioSender &) = delete;
  GstAudioSender &operator=(const GstAudioSender &) = delete;

  /// Build and start the capture/send pipeline. Returns false on failure.
  bool start();

  /// Stop and tear down the pipeline.
  void stop();

private:
  std::string buildLaunch(const char *sourceDescription) const;

  std::string m_serverHost;
  uint16_t m_rtpPort;

  std::unique_ptr<GstAudioPipeline> m_pipeline;
#if defined(__APPLE__)
  std::unique_ptr<MacAudioCapture> m_macCapture;
#endif
};
