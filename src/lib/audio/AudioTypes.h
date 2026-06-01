/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>

/// Default TCP port the AudioServer listens on (separate from the control port).
static constexpr uint16_t kDefaultAudioPort = 24801;

/// PCM sample rate used throughout the audio pipeline.
static constexpr int kAudioSampleRate = 48000;

/// Number of PCM channels (stereo).
static constexpr int kAudioChannels = 2;

/// Opus frame size in samples (20 ms at 48 kHz).
static constexpr int kAudioFrameSize = 960;

/// Target Opus encode bitrate in bits per second.
static constexpr int kAudioBitrate = 96000;

/// Maximum encoded Opus packet size in bytes (safe upper bound for one frame).
static constexpr int kAudioMaxPacketBytes = 4000;

/// Magic bytes sent by the audio client at the start of the audio TCP connection.
static constexpr char kAudioHandshakeMagic[] = "DSKFAUDIO";
static constexpr int kAudioHandshakeMagicLen = 9;

/// Server reply on successful handshake.
static constexpr char kAudioHandshakeOk[] = "OK";
/// Server reply when the handshake is rejected.
static constexpr char kAudioHandshakeErr[] = "ER";
static constexpr int kAudioHandshakeReplyLen = 2;
