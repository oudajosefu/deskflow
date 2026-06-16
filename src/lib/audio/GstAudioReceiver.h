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

///
/// Server-side audio receiver: one per connected audio client. Receives the
/// client's RTP/Opus stream on a dedicated UDP port and plays it on the server's
/// selected output device.
///
///   udpsrc port=<P> caps="...OPUS..."
///     ! rtpjitterbuffer latency=<ms> do-lost=true
///     ! rtpopusdepay ! opusdec plc=true use-inband-fec=true
///     ! audioconvert ! audioresample
///     ! level ! volume name=vol ! <platform-sink>
///
/// The jitter buffer absorbs network timing variation, opusdec PLC conceals lost
/// packets, and audioresample matches the output device's native rate. The
/// `volume` element and the sink's device are the runtime knobs the GUI drives.
///
class GstAudioReceiver
{
public:
  explicit GstAudioReceiver(uint16_t rtpPort);
  ~GstAudioReceiver();

  GstAudioReceiver(const GstAudioReceiver &) = delete;
  GstAudioReceiver &operator=(const GstAudioReceiver &) = delete;

  /// Build and start the receive/playback pipeline. Returns false on failure.
  bool start();

  /// Stop and tear down the pipeline.
  void stop();

  [[nodiscard]] uint16_t rtpPort() const
  {
    return m_rtpPort;
  }

  /// Linear playback volume, 1.0 = unity. Applied live.
  void setVolume(double volume);

  /// Mute/unmute playback. Applied live.
  void setMute(bool mute);

  /// Select the output device by its GStreamer element "device" id (empty = system
  /// default via autoaudiosink). Rebuilds the pipeline if currently running, since
  /// most audio sinks cannot change device while PLAYING.
  void setOutputDeviceId(const std::string &deviceId);

  /// Toggle low-latency playback. When on, the sink uses a small ring buffer and
  /// the jitter buffer a shorter playout delay (less latency, less jitter
  /// tolerance); when off, the original robust buffering is used. Rebuilds the
  /// pipeline if currently running, since the sink/buffer sizing is fixed at build.
  void setLowLatency(bool lowLatency);

private:
  std::string buildLaunch() const;
  std::string sinkDescription() const;

  uint16_t m_rtpPort;
  double m_volume = 1.0;
  bool m_mute = false;
  std::string m_deviceId; // empty => system default
  bool m_lowLatency = true;
  bool m_forceDefaultSink = false; // set after a low-latency sink build failure

  bool m_running = false;
  std::unique_ptr<GstAudioPipeline> m_pipeline;
};
