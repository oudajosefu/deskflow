/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <string>

//
// Audio routing protocol constants.
//
// Two planes:
//   * Control plane (TCP, kDefaultAudioPort): handshake + per-client RTP port
//     negotiation. Reliable + low-rate, so TCP is the right fit.
//   * Media plane (RTP over UDP, port assigned per client): the actual audio,
//     driven entirely by GStreamer (udpsink -> udpsrc with rtpjitterbuffer).
//

/// Default TCP port the AudioServer control plane listens on (separate from the control port).
static constexpr uint16_t kDefaultAudioPort = 24801;

/// First UDP port the server hands out for RTP media streams; one per client, incrementing.
static constexpr uint16_t kAudioRtpPortBase = 24810;

/// PCM sample rate used throughout the audio pipeline.
static constexpr int kAudioSampleRate = 48000;

/// Number of PCM channels (stereo).
static constexpr int kAudioChannels = 2;

/// Target Opus encode bitrate in bits per second.
static constexpr int kAudioBitrate = 96000;

/// Opus frame size in milliseconds (lower = less latency, more overhead).
static constexpr int kAudioOpusFrameMs = 20;

/// RTP dynamic payload type used for the Opus stream (must match on both ends).
static constexpr int kAudioRtpPayloadType = 96;

/// Jitter-buffer playout delay in milliseconds on the receive side. The "robust"
/// value tolerates more network jitter; the "low" value trims latency for the
/// low-latency playback mode (suited to a wired LAN / solid WiFi).
static constexpr int kAudioJitterBufferMs = 50;
static constexpr int kAudioJitterBufferLowMs = 30;

/// Low-latency playback sink ring-buffer sizing (microseconds). buffer-time caps
/// how much audio the sink queues ahead; latency-time is one device period.
/// These replace the audio sink's ~200 ms default ring buffer, which is the
/// dominant (and media-dependent) source of end-to-end latency.
static constexpr int kAudioSinkBufferTimeUs = 40000;
static constexpr int kAudioSinkLatencyTimeUs = 10000;

/// Capture-side leaky queue depth in milliseconds. The queue only decouples the
/// live source from the encoder, so this just bounds worst-case latency during a
/// transient stall (it already drops oldest buffers — see leaky=downstream).
static constexpr int kAudioCaptureQueueMs = 30;

/// Magic bytes sent by the audio client at the start of the control connection.
static constexpr char kAudioHandshakeMagic[] = "DSKFAUDIO";
static constexpr int kAudioHandshakeMagicLen = 9;

/// Server reply tags on the control connection.
/// Success reply layout: "OK" + uint16 big-endian RTP UDP port the client should stream to.
static constexpr char kAudioHandshakeOk[] = "OK";
/// Failure reply: server could not start a receiver for this client.
static constexpr char kAudioHandshakeErr[] = "ER";
static constexpr int kAudioHandshakeReplyTagLen = 2;
static constexpr int kAudioHandshakeOkReplyLen = kAudioHandshakeReplyTagLen + 2; // tag + uint16 port

///
/// Build the RTP caps string that the receiving udpsrc must advertise so the
/// jitter buffer / depayloader / decoder know how to interpret the stream.
/// Must describe the exact same format the sender's rtpopuspay produces.
///
inline std::string audioRtpCaps()
{
  // encoding-params + sprop-stereo tell the Opus depay/decoder this is stereo.
  return "application/x-rtp,media=(string)audio,clock-rate=(int)" + std::to_string(kAudioSampleRate) +
         ",encoding-name=(string)OPUS,payload=(int)" + std::to_string(kAudioRtpPayloadType) +
         ",encoding-params=(string)" + std::to_string(kAudioChannels) + ",sprop-stereo=(int)1";
}
