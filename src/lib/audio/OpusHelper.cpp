/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "audio/OpusHelper.h"

#include "base/Log.h"

#include <opus.h>

OpusEncoderWrapper::OpusEncoderWrapper()
{
  int err = OPUS_OK;
  m_encoder = opus_encoder_create(kAudioSampleRate, kAudioChannels, OPUS_APPLICATION_AUDIO, &err);
  if (err != OPUS_OK || m_encoder == nullptr) {
    LOG_ERR("opus encoder create failed: %s", opus_strerror(err));
    m_encoder = nullptr;
    return;
  }
  opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(kAudioBitrate));
}

OpusEncoderWrapper::~OpusEncoderWrapper()
{
  if (m_encoder != nullptr) {
    opus_encoder_destroy(m_encoder);
  }
}

std::vector<uint8_t> OpusEncoderWrapper::encode(const float *pcm)
{
  if (m_encoder == nullptr) {
    return {};
  }

  std::vector<uint8_t> packet(kAudioMaxPacketBytes);
  const opus_int32 bytes =
      opus_encode_float(m_encoder, pcm, kAudioFrameSize, packet.data(), static_cast<opus_int32>(packet.size()));
  if (bytes < 0) {
    LOG_ERR("opus encode failed: %s", opus_strerror(bytes));
    return {};
  }
  packet.resize(static_cast<size_t>(bytes));
  return packet;
}

OpusDecoderWrapper::OpusDecoderWrapper()
{
  int err = OPUS_OK;
  m_decoder = opus_decoder_create(kAudioSampleRate, kAudioChannels, &err);
  if (err != OPUS_OK || m_decoder == nullptr) {
    LOG_ERR("opus decoder create failed: %s", opus_strerror(err));
    m_decoder = nullptr;
  }
}

OpusDecoderWrapper::~OpusDecoderWrapper()
{
  if (m_decoder != nullptr) {
    opus_decoder_destroy(m_decoder);
  }
}

std::vector<float> OpusDecoderWrapper::decode(const uint8_t *data, size_t length)
{
  if (m_decoder == nullptr) {
    return {};
  }

  std::vector<float> pcm(static_cast<size_t>(kAudioFrameSize * kAudioChannels));
  const int frames =
      opus_decode_float(m_decoder, data, static_cast<opus_int32>(length), pcm.data(), kAudioFrameSize, 0);
  if (frames < 0) {
    LOG_ERR("opus decode failed: %s", opus_strerror(frames));
    return {};
  }
  pcm.resize(static_cast<size_t>(frames * kAudioChannels));
  return pcm;
}
