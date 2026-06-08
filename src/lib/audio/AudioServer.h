/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

#include <memory>

class IAudioPlayback;
class OpusDecoderWrapper;

///
/// Listens on the audio TCP port. When a client connects and completes the
/// DSKFAUDIO handshake, it starts receiving Opus-encoded audio packets and
/// playing them through the local audio output.
///
/// One AudioServer instance services all connected audio clients (mixing is
/// first-writer-wins; only the first connected client is played at a time).
///
class AudioServer : public QObject
{
  Q_OBJECT

public:
  explicit AudioServer(quint16 port, QObject *parent = nullptr);
  ~AudioServer() override;

  /// Start listening. Returns false if the port cannot be bound.
  bool listen();

  /// Stop listening and disconnect all audio clients.
  void close();

  [[nodiscard]] quint16 port() const
  {
    return m_port;
  }

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
    std::unique_ptr<OpusDecoderWrapper> decoder;
    std::unique_ptr<IAudioPlayback> playback;
    quint64 packetsReceived = 0;
    quint64 framesPlayed = 0;
    quint64 lastLoggedPackets = 0;
  };

  void processHandshake(ClientSession &session);
  void processAudioData(ClientSession &session);
  void startPlayback(ClientSession &session);

  quint16 m_port;
  QTcpServer *m_server = nullptr;
  QList<ClientSession *> m_sessions;
};
