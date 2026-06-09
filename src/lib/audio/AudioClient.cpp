/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/AudioClient.h"

#include "audio/AudioTypes.h"
#include "audio/GstAudioSender.h"
#include "base/Log.h"

#include <QTcpSocket>

#include <cstring>

AudioClient::AudioClient(const QString &serverHost, quint16 port, const QString &clientName, QObject *parent)
    : QObject(parent),
      m_serverHost(serverHost),
      m_port(port),
      m_clientName(clientName)
{
}

AudioClient::~AudioClient()
{
  stop();
}

void AudioClient::start()
{
  if (m_running) {
    return;
  }
  m_running = true;
  m_thread = QThread::create([this] { runControl(); });
  m_thread->setObjectName(QStringLiteral("AudioControl"));
  m_thread->start();
}

void AudioClient::stop()
{
  m_running = false;
  if (m_thread != nullptr) {
    m_thread->wait(3000);
    m_thread->deleteLater();
    m_thread = nullptr;
  }
}

void AudioClient::runControl()
{
  // Blocking control I/O happens here, off the Qt event loop.
  QTcpSocket socket;
  socket.connectToHost(m_serverHost, m_port);
  if (!socket.waitForConnected(5000)) {
    LOG_ERR("AudioClient: cannot connect to %s:%d", qPrintable(m_serverHost), m_port);
    m_running = false;
    return;
  }

  // Handshake: magic + 2-byte name length + name (UTF-8).
  const QByteArray nameBytes = m_clientName.toUtf8();
  const auto nameLen = static_cast<uint16_t>(nameBytes.size());
  QByteArray handshake;
  handshake.reserve(kAudioHandshakeMagicLen + 2 + nameBytes.size());
  handshake.append(kAudioHandshakeMagic, kAudioHandshakeMagicLen);
  handshake.append(static_cast<char>(nameLen >> 8));
  handshake.append(static_cast<char>(nameLen & 0xFF));
  handshake.append(nameBytes);
  socket.write(handshake);
  socket.flush();

  // Reply is either "ER" or "OK" + uint16 big-endian RTP UDP port.
  QByteArray reply;
  while (reply.size() < kAudioHandshakeOkReplyLen && socket.state() == QAbstractSocket::ConnectedState) {
    if (!socket.waitForReadyRead(5000)) {
      break;
    }
    reply.append(socket.readAll());
    if (reply.size() >= kAudioHandshakeReplyTagLen &&
        std::memcmp(reply.constData(), kAudioHandshakeErr, kAudioHandshakeReplyTagLen) == 0) {
      LOG_ERR("AudioClient: server rejected audio handshake");
      m_running = false;
      return;
    }
  }

  if (reply.size() < kAudioHandshakeOkReplyLen ||
      std::memcmp(reply.constData(), kAudioHandshakeOk, kAudioHandshakeReplyTagLen) != 0) {
    LOG_ERR("AudioClient: incomplete or invalid handshake reply");
    m_running = false;
    return;
  }

  const auto rtpPort = static_cast<quint16>(
      static_cast<uint8_t>(reply[kAudioHandshakeReplyTagLen]) << 8 |
      static_cast<uint8_t>(reply[kAudioHandshakeReplyTagLen + 1])
  );

  LOG_INFO("AudioClient: server assigned RTP port %u, starting capture", rtpPort);

  m_sender = std::make_unique<GstAudioSender>(m_serverHost.toStdString(), rtpPort);
  if (!m_sender->start()) {
    LOG_ERR("AudioClient: failed to start audio capture/stream");
    m_sender.reset();
    m_running = false;
    return;
  }

  // GStreamer streams the media on its own threads; this thread just keeps the
  // control connection alive and watches for shutdown or server disconnect.
  while (m_running && socket.state() == QAbstractSocket::ConnectedState) {
    socket.waitForReadyRead(200); // wakes on server FIN/keepalive; 200 ms cap bounds shutdown latency
  }

  LOG_INFO("AudioClient: stopping audio stream");
  m_sender->stop();
  m_sender.reset();
  socket.disconnectFromHost();
}
