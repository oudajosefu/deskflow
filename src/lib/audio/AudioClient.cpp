/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/AudioClient.h"

#include "audio/AudioTypes.h"
#include "audio/IAudioCapture.h"
#include "audio/OpusHelper.h"
#include "base/Log.h"

#if defined(Q_OS_WIN)
#include "audio/WinAudioCapture.h"
#elif defined(Q_OS_MAC)
#include "audio/MacAudioCapture.h"
#else
#include "audio/LinuxAudioCapture.h"
#endif

#include <QTcpSocket>

static std::unique_ptr<IAudioCapture> makePlatformCapture()
{
#if defined(Q_OS_WIN)
  return std::make_unique<WinAudioCapture>();
#elif defined(Q_OS_MAC)
  return std::make_unique<MacAudioCapture>();
#else
  return std::make_unique<LinuxAudioCapture>();
#endif
}

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
  m_thread = QThread::create([this] { runCapture(); });
  m_thread->setObjectName(QStringLiteral("AudioCapture"));
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

void AudioClient::runCapture()
{
  // All blocking I/O happens here, off the Qt event loop.
  QTcpSocket socket;
  socket.connectToHost(m_serverHost, m_port);
  if (!socket.waitForConnected(5000)) {
    LOG_ERR("AudioClient: cannot connect to %s:%d", qPrintable(m_serverHost), m_port);
    m_running = false;
    return;
  }

  // Handshake: magic + 2-byte name length + name
  const QByteArray nameBytes = m_clientName.toUtf8();
  const uint16_t nameLen = static_cast<uint16_t>(nameBytes.size());
  QByteArray handshake;
  handshake.reserve(kAudioHandshakeMagicLen + 2 + nameBytes.size());
  handshake.append(kAudioHandshakeMagic, kAudioHandshakeMagicLen);
  handshake.append(static_cast<char>(nameLen >> 8));
  handshake.append(static_cast<char>(nameLen & 0xFF));
  handshake.append(nameBytes);
  socket.write(handshake);
  socket.flush();

  if (!socket.waitForReadyRead(5000)) {
    LOG_ERR("AudioClient: handshake timeout");
    m_running = false;
    return;
  }
  const QByteArray reply = socket.read(kAudioHandshakeReplyLen);
  if (reply != QByteArray(kAudioHandshakeOk, kAudioHandshakeReplyLen)) {
    LOG_ERR("AudioClient: server rejected audio handshake");
    m_running = false;
    return;
  }

  LOG_INFO("AudioClient: connected to audio server, starting capture");

  m_capture = makePlatformCapture();
  if (!m_capture->start()) {
    LOG_ERR("AudioClient: failed to start audio capture");
    m_running = false;
    return;
  }

  m_encoder = std::make_unique<OpusEncoderWrapper>();
  if (!m_encoder->isValid()) {
    LOG_ERR("AudioClient: failed to create Opus encoder");
    m_capture->stop();
    m_running = false;
    return;
  }

  std::vector<float> pcmBuf(static_cast<size_t>(kAudioFrameSize * kAudioChannels));

  while (m_running && socket.state() == QAbstractSocket::ConnectedState) {
    const size_t got = m_capture->readFrames(pcmBuf.data(), kAudioFrameSize);
    if (got < static_cast<size_t>(kAudioFrameSize)) {
      continue; // not enough data yet
    }

    auto packet = m_encoder->encode(pcmBuf.data());
    if (packet.empty()) {
      continue;
    }

    const uint16_t pktLen = static_cast<uint16_t>(packet.size());
    const char header[2] = {static_cast<char>(pktLen >> 8), static_cast<char>(pktLen & 0xFF)};
    socket.write(header, 2);
    socket.write(reinterpret_cast<const char *>(packet.data()), static_cast<qint64>(packet.size()));
    socket.flush();
  }

  m_capture->stop();
  m_capture.reset();
  m_encoder.reset();
  socket.disconnectFromHost();
  LOG_INFO("AudioClient: capture stream stopped");
}
