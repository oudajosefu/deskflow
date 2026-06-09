/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <memory>

class GstAudioReceiver;

///
/// Server-side audio control plane.
///
/// Listens on the audio control port (TCP). When a client connects and completes
/// the DSKFAUDIO handshake, the server allocates a free UDP port, starts a
/// GstAudioReceiver on it (which plays the client's RTP/Opus stream), and tells
/// the client which port to stream to.
///
/// One AudioServer services all connected audio clients; each gets its own
/// receiver pipeline (with independent volume/mute/output device), so multiple
/// clients simply mix at the chosen output device. Receivers are addressed by
/// client (screen) name so the server GUI can control each independently.
///
class AudioServer : public QObject
{
  Q_OBJECT

public:
  explicit AudioServer(quint16 port, QObject *parent = nullptr);
  ~AudioServer() override;

  /// Start listening. Returns false if the control port cannot be bound.
  bool listen();

  /// Stop listening and disconnect all audio clients.
  void close();

  [[nodiscard]] quint16 port() const
  {
    return m_port;
  }

  /// Per-client playback controls (applied live to that client's receiver).
  void setClientVolume(const QString &clientName, double volume);
  void setClientMute(const QString &clientName, bool mute);
  void setClientOutputDevice(const QString &clientName, const QString &deviceId);

Q_SIGNALS:
  /// Emitted (on the AudioServer thread) when a client's playback pipeline starts
  /// or stops, so the app/GUI can apply persisted settings and update status.
  void clientAudioStarted(const QString &clientName);
  void clientAudioStopped(const QString &clientName);

private Q_SLOTS:
  void onNewConnection();
  void onClientReadyRead();
  void onClientDisconnected();

private:
  struct ClientSession
  {
    QTcpSocket *socket = nullptr;
    bool handshakeDone = false;
    QByteArray buffer;
    QString clientName;
    quint16 rtpPort = 0;
    std::unique_ptr<GstAudioReceiver> receiver;
  };

  void processHandshake(ClientSession &session);
  quint16 allocateRtpPort() const;
  ClientSession *sessionFor(const QString &clientName) const;

  quint16 m_port;
  QTcpServer *m_server = nullptr;
  QList<ClientSession *> m_sessions;
};
