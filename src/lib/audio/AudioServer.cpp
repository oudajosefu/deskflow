/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/AudioServer.h"

#include "audio/AudioTypes.h"
#include "audio/IAudioPlayback.h"
#include "audio/OpusHelper.h"
#include "base/Log.h"

#if defined(Q_OS_WIN)
#include "audio/WinAudioPlayback.h"
#elif defined(Q_OS_MAC)
#include "audio/MacAudioPlayback.h"
#else
#include "audio/LinuxAudioPlayback.h"
#endif

#include <QHostAddress>

static std::unique_ptr<IAudioPlayback> makePlatformPlayback()
{
#if defined(Q_OS_WIN)
  return std::make_unique<WinAudioPlayback>();
#elif defined(Q_OS_MAC)
  return std::make_unique<MacAudioPlayback>();
#else
  return std::make_unique<LinuxAudioPlayback>();
#endif
}

AudioServer::AudioServer(quint16 port, QObject *parent) : QObject(parent), m_port(port)
{
}

AudioServer::~AudioServer()
{
  close();
}

bool AudioServer::listen()
{
  m_server = new QTcpServer(this);
  connect(m_server, &QTcpServer::newConnection, this, &AudioServer::onNewConnection);

  if (!m_server->listen(QHostAddress::Any, m_port)) {
    LOG_ERR("AudioServer: cannot bind to port %d: %s", m_port, qPrintable(m_server->errorString()));
    return false;
  }

  LOG_INFO("AudioServer: listening on port %d", m_port);
  return true;
}

void AudioServer::close()
{
  if (m_server != nullptr) {
    m_server->close();
  }
  for (auto *session : m_sessions) {
    if (session->playback) {
      session->playback->stop();
    }
    session->socket->disconnectFromHost();
    delete session;
  }
  m_sessions.clear();
}

void AudioServer::onNewConnection()
{
  while (m_server->hasPendingConnections()) {
    auto *sock = m_server->nextPendingConnection();
    auto *session = new ClientSession;
    session->socket = sock;
    session->decoder = std::make_unique<OpusDecoderWrapper>();
    m_sessions.append(session);

    connect(sock, &QTcpSocket::readyRead, this, &AudioServer::onClientReadyRead);
    connect(sock, &QTcpSocket::disconnected, this, &AudioServer::onClientDisconnected);
    LOG_INFO("AudioServer: new connection from %s", qPrintable(sock->peerAddress().toString()));
  }
}

void AudioServer::onClientReadyRead()
{
  auto *sock = qobject_cast<QTcpSocket *>(sender());
  if (sock == nullptr) {
    return;
  }

  auto it =
      std::find_if(m_sessions.begin(), m_sessions.end(), [sock](const ClientSession *s) { return s->socket == sock; });
  if (it == m_sessions.end()) {
    return;
  }

  ClientSession &session = **it;
  session.buffer.append(sock->readAll());

  if (!session.handshakeDone) {
    processHandshake(session);
  } else {
    processAudioData(session);
  }
}

void AudioServer::onClientDisconnected()
{
  auto *sock = qobject_cast<QTcpSocket *>(sender());
  if (sock == nullptr) {
    return;
  }

  auto it =
      std::find_if(m_sessions.begin(), m_sessions.end(), [sock](const ClientSession *s) { return s->socket == sock; });
  if (it == m_sessions.end()) {
    return;
  }

  ClientSession *session = *it;
  LOG_INFO("AudioServer: client disconnected");
  if (session->playback) {
    session->playback->stop();
  }
  m_sessions.erase(it);
  delete session;
  sock->deleteLater();
}

void AudioServer::processHandshake(ClientSession &session)
{
  // Expect: kAudioHandshakeMagicLen bytes magic + 2-byte name length + name
  if (session.buffer.size() < kAudioHandshakeMagicLen + 2) {
    return;
  }

  if (std::memcmp(session.buffer.constData(), kAudioHandshakeMagic, kAudioHandshakeMagicLen) != 0) {
    LOG_WARN("AudioServer: invalid handshake magic, dropping connection");
    session.socket->write(kAudioHandshakeErr, kAudioHandshakeReplyLen);
    session.socket->disconnectFromHost();
    return;
  }

  const uint16_t nameLen = static_cast<uint16_t>(
      static_cast<uint8_t>(session.buffer[kAudioHandshakeMagicLen]) << 8 |
      static_cast<uint8_t>(session.buffer[kAudioHandshakeMagicLen + 1])
  );

  const int totalHandshake = kAudioHandshakeMagicLen + 2 + nameLen;
  if (session.buffer.size() < totalHandshake) {
    return; // wait for more data
  }

  const QString clientName = QString::fromUtf8(session.buffer.constData() + kAudioHandshakeMagicLen + 2, nameLen);
  session.buffer.remove(0, totalHandshake);
  session.handshakeDone = true;

  session.socket->write(kAudioHandshakeOk, kAudioHandshakeReplyLen);
  LOG_INFO("AudioServer: audio stream from client \"%s\"", qPrintable(clientName));
  startPlayback(session);

  // There may already be audio data in the buffer
  if (!session.buffer.isEmpty()) {
    processAudioData(session);
  }
}

void AudioServer::processAudioData(ClientSession &session)
{
  // Packet framing: uint16 length (big-endian) + Opus bytes
  while (session.buffer.size() >= 2) {
    const uint16_t packetLen =
        static_cast<uint16_t>(static_cast<uint8_t>(session.buffer[0]) << 8 | static_cast<uint8_t>(session.buffer[1]));

    if (packetLen == 0 || packetLen > kAudioMaxPacketBytes) {
      LOG_WARN("AudioServer: invalid packet length %d, dropping connection", packetLen);
      session.socket->disconnectFromHost();
      return;
    }

    if (session.buffer.size() < 2 + packetLen) {
      break; // incomplete packet, wait for more data
    }

    const auto *opusData = reinterpret_cast<const uint8_t *>(session.buffer.constData() + 2);
    auto pcm = session.decoder->decode(opusData, packetLen);
    session.buffer.remove(0, 2 + packetLen);
    ++session.packetsReceived;

    if (!pcm.empty() && session.playback) {
      const size_t frames = pcm.size() / static_cast<size_t>(kAudioChannels);
      session.playback->writeFrames(pcm.data(), frames);
      session.framesPlayed += frames;
    }
  }

  // Throttled progress log (~every 50 packets, i.e. ~1 s of audio) so the receive/decode/play path is observable.
  if (session.packetsReceived - session.lastLoggedPackets >= 50) {
    session.lastLoggedPackets = session.packetsReceived;
    LOG_DEBUG(
        "AudioServer: receiving — %llu packets decoded, %llu frames played",
        static_cast<unsigned long long>(session.packetsReceived), static_cast<unsigned long long>(session.framesPlayed)
    );
  }
}

void AudioServer::startPlayback(ClientSession &session)
{
  session.playback = makePlatformPlayback();
  if (!session.playback->start()) {
    LOG_ERR("AudioServer: failed to start platform audio playback");
    session.playback.reset();
  }
}
