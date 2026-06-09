/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>

class GstAudioSender;

///
/// Client-side audio control plane.
///
/// Connects to the server's audio control port, performs the DSKFAUDIO
/// handshake and learns which UDP port to stream RTP/Opus audio to. It then
/// starts a GstAudioSender, which captures system-output audio and streams it
/// to the server entirely inside GStreamer.
///
/// The control socket runs on a dedicated QThread (blocking handshake, then a
/// liveness wait) so it never assumes a Qt event loop on the core thread. The
/// media itself flows over UDP, serviced by GStreamer's own threads.
///
class AudioClient : public QObject
{
  Q_OBJECT

public:
  explicit AudioClient(const QString &serverHost, quint16 port, const QString &clientName, QObject *parent = nullptr);
  ~AudioClient() override;

  /// Connect and start streaming. Non-blocking — work happens on the worker thread.
  void start();

  /// Stop streaming and disconnect.
  void stop();

private:
  void runControl(); // executed on m_thread

  QString m_serverHost;
  quint16 m_port;
  QString m_clientName;

  QThread *m_thread = nullptr;
  std::atomic<bool> m_running{false};

  std::unique_ptr<GstAudioSender> m_sender;
};
