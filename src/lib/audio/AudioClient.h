/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QThread>

#include <atomic>
#include <memory>

class IAudioCapture;
class OpusEncoderWrapper;

///
/// Connects to the server's audio port, performs the DSKFAUDIO handshake,
/// and streams Opus-encoded loopback audio from the local machine.
///
/// The capture + encode + send loop runs on a dedicated QThread so it does not
/// block the Qt event loop.
///
class AudioClient : public QObject
{
  Q_OBJECT

public:
  explicit AudioClient(const QString &serverHost, quint16 port, const QString &clientName, QObject *parent = nullptr);
  ~AudioClient() override;

  /// Connect and start streaming. Non-blocking — actual work happens on the worker thread.
  void start();

  /// Stop streaming and disconnect.
  void stop();

private:
  void runCapture(); // executed on m_thread

  QString m_serverHost;
  quint16 m_port;
  QString m_clientName;

  QThread *m_thread = nullptr;
  std::atomic<bool> m_running{false};

  std::unique_ptr<IAudioCapture> m_capture;
  std::unique_ptr<OpusEncoderWrapper> m_encoder;
};
