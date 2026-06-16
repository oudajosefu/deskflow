/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/AudioServer.h"

#include "audio/AudioTypes.h"
#include "audio/GstAudioReceiver.h"
#include "base/Log.h"

#include <QHostAddress>

#include <algorithm>
#include <cstring>

AudioServer::AudioServer(quint16 port, QObject *parent) : QObject(parent), m_port(port)
{
}

AudioServer::~AudioServer()
{
  close();
}

void AudioServer::start()
{
  if (m_thread != nullptr) {
    return;
  }
  // Run the control plane on its own thread with a Qt event loop (QThread's
  // default run() calls exec()). The Deskflow core thread we are constructed on
  // runs a native EventQueue loop, not a Qt one, so QTcpServer/QTcpSocket signals
  // would never be delivered there. Moving to a dedicated thread fixes that;
  // listen() then runs on the worker so m_server is created with the right
  // thread affinity.
  m_thread = new QThread;
  m_thread->setObjectName(QStringLiteral("AudioControl"));
  moveToThread(m_thread);
  connect(m_thread, &QThread::started, this, &AudioServer::listen);
  m_thread->start();
}

void AudioServer::listen()
{
  m_server = new QTcpServer(this);
  connect(m_server, &QTcpServer::newConnection, this, &AudioServer::onNewConnection);

  if (!m_server->listen(QHostAddress::Any, m_port)) {
    LOG_ERR("AudioServer: cannot bind to port %d: %s", m_port, qPrintable(m_server->errorString()));
    return;
  }

  LOG_INFO("AudioServer: control plane listening on port %d", m_port);
}

void AudioServer::shutdown()
{
  // Runs on the audio thread: every object torn down here was created there.
  if (m_server != nullptr) {
    m_server->close();
  }
  for (auto *session : m_sessions) {
    if (session->receiver) {
      session->receiver->stop();
    }
    if (session->socket != nullptr) {
      session->socket->disconnectFromHost();
    }
    delete session;
  }
  m_sessions.clear();
  delete m_server; // also deletes its child sockets
  m_server = nullptr;
}

void AudioServer::close()
{
  if (m_thread == nullptr) {
    return;
  }
  // Tear down on the audio thread, then stop and join it.
  QMetaObject::invokeMethod(this, &AudioServer::shutdown, Qt::BlockingQueuedConnection);
  m_thread->quit();
  m_thread->wait();
  delete m_thread;
  m_thread = nullptr;
}

quint16 AudioServer::allocateRtpPort() const
{
  // Lowest unused port at/above the base; one media stream per connected client.
  for (quint16 candidate = kAudioRtpPortBase; candidate < kAudioRtpPortBase + 100; ++candidate) {
    const bool inUse = std::any_of(m_sessions.begin(), m_sessions.end(), [candidate](const ClientSession *s) {
      return s->rtpPort == candidate;
    });
    if (!inUse) {
      return candidate;
    }
  }
  return 0; // exhausted
}

AudioServer::ClientSession *AudioServer::sessionFor(const QString &clientName) const
{
  auto it = std::find_if(m_sessions.begin(), m_sessions.end(), [&clientName](const ClientSession *s) {
    return s->handshakeDone && s->clientName == clientName;
  });
  return it == m_sessions.end() ? nullptr : *it;
}

void AudioServer::onNewConnection()
{
  while (m_server->hasPendingConnections()) {
    auto *sock = m_server->nextPendingConnection();
    auto *session = new ClientSession;
    session->socket = sock;
    m_sessions.append(session);

    connect(sock, &QTcpSocket::readyRead, this, &AudioServer::onClientReadyRead);
    connect(sock, &QTcpSocket::disconnected, this, &AudioServer::onClientDisconnected);
    LOG_INFO("AudioServer: new control connection from %s", qPrintable(sock->peerAddress().toString()));
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

  // After the handshake there is nothing more to read on the control channel
  // (media flows over UDP); we just keep the connection open as a liveness link.
  if (!session.handshakeDone) {
    processHandshake(session);
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
  LOG_INFO("AudioServer: audio client \"%s\" disconnected", qPrintable(session->clientName));
  if (session->receiver) {
    session->receiver->stop();
  }
  const QString name = session->clientName;
  m_sessions.erase(it);
  delete session;
  sock->deleteLater();

  if (!name.isEmpty()) {
    Q_EMIT clientAudioStopped(name);
  }
}

void AudioServer::processHandshake(ClientSession &session)
{
  // Expect: magic + 2-byte name length + name.
  if (session.buffer.size() < kAudioHandshakeMagicLen + 2) {
    return;
  }

  if (std::memcmp(session.buffer.constData(), kAudioHandshakeMagic, kAudioHandshakeMagicLen) != 0) {
    LOG_WARN("AudioServer: invalid handshake magic, dropping connection");
    session.socket->write(kAudioHandshakeErr, kAudioHandshakeReplyTagLen);
    session.socket->disconnectFromHost();
    return;
  }

  const auto nameLen = static_cast<uint16_t>(
      static_cast<uint8_t>(session.buffer[kAudioHandshakeMagicLen]) << 8 |
      static_cast<uint8_t>(session.buffer[kAudioHandshakeMagicLen + 1])
  );

  const int totalHandshake = kAudioHandshakeMagicLen + 2 + nameLen;
  if (session.buffer.size() < totalHandshake) {
    return; // wait for more data
  }

  session.clientName = QString::fromUtf8(session.buffer.constData() + kAudioHandshakeMagicLen + 2, nameLen);
  session.buffer.remove(0, totalHandshake);
  session.handshakeDone = true;

  const quint16 rtpPort = allocateRtpPort();
  session.rtpPort = rtpPort;
  if (rtpPort != 0) {
    session.receiver = std::make_unique<GstAudioReceiver>(rtpPort);
    if (!session.receiver->start()) {
      session.receiver.reset();
      session.rtpPort = 0;
    }
  }

  if (session.receiver) {
    // Reply: "OK" + uint16 big-endian RTP port.
    QByteArray reply;
    reply.append(kAudioHandshakeOk, kAudioHandshakeReplyTagLen);
    reply.append(static_cast<char>(rtpPort >> 8));
    reply.append(static_cast<char>(rtpPort & 0xFF));
    session.socket->write(reply);
    session.socket->flush();
    LOG_INFO("AudioServer: client \"%s\" -> RTP port %u", qPrintable(session.clientName), rtpPort);
    Q_EMIT clientAudioStarted(session.clientName);
  } else {
    LOG_ERR("AudioServer: could not start receiver for \"%s\"", qPrintable(session.clientName));
    session.socket->write(kAudioHandshakeErr, kAudioHandshakeReplyTagLen);
    session.socket->flush();
    session.socket->disconnectFromHost();
  }
}

void AudioServer::setClientVolume(const QString &clientName, double volume)
{
  // Marshal onto the audio thread, which owns m_sessions and the receivers.
  QMetaObject::invokeMethod(
      this,
      [this, clientName, volume] {
        if (ClientSession *s = sessionFor(clientName); s != nullptr && s->receiver) {
          s->receiver->setVolume(volume);
        }
      },
      Qt::QueuedConnection
  );
}

void AudioServer::setClientMute(const QString &clientName, bool mute)
{
  QMetaObject::invokeMethod(
      this,
      [this, clientName, mute] {
        if (ClientSession *s = sessionFor(clientName); s != nullptr && s->receiver) {
          s->receiver->setMute(mute);
        }
      },
      Qt::QueuedConnection
  );
}

void AudioServer::setClientOutputDevice(const QString &clientName, const QString &deviceId)
{
  QMetaObject::invokeMethod(
      this,
      [this, clientName, deviceId] {
        if (ClientSession *s = sessionFor(clientName); s != nullptr && s->receiver) {
          s->receiver->setOutputDeviceId(deviceId.toStdString());
        }
      },
      Qt::QueuedConnection
  );
}

void AudioServer::setClientLowLatency(const QString &clientName, bool lowLatency)
{
  QMetaObject::invokeMethod(
      this,
      [this, clientName, lowLatency] {
        if (ClientSession *s = sessionFor(clientName); s != nullptr && s->receiver) {
          s->receiver->setLowLatency(lowLatency);
        }
      },
      Qt::QueuedConnection
  );
}
