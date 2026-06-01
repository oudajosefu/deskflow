/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "audio/AudioTypes.h"

#include <cstdint>
#include <memory>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

/// RAII wrappers around the Opus encoder and decoder.
class OpusEncoderWrapper
{
public:
  OpusEncoderWrapper();
  ~OpusEncoderWrapper();

  OpusEncoderWrapper(const OpusEncoderWrapper &) = delete;
  OpusEncoderWrapper &operator=(const OpusEncoderWrapper &) = delete;

  bool isValid() const
  {
    return m_encoder != nullptr;
  }

  /// Encode one frame of interleaved float32 PCM (kAudioFrameSize samples per channel).
  /// Returns the compressed packet bytes, or empty on failure.
  std::vector<uint8_t> encode(const float *pcm);

private:
  OpusEncoder *m_encoder = nullptr;
};

class OpusDecoderWrapper
{
public:
  OpusDecoderWrapper();
  ~OpusDecoderWrapper();

  OpusDecoderWrapper(const OpusDecoderWrapper &) = delete;
  OpusDecoderWrapper &operator=(const OpusDecoderWrapper &) = delete;

  bool isValid() const
  {
    return m_decoder != nullptr;
  }

  /// Decode one Opus packet into interleaved float32 PCM.
  /// Returns kAudioFrameSize * kAudioChannels floats, or empty on failure.
  std::vector<float> decode(const uint8_t *data, size_t length);

private:
  OpusDecoder *m_decoder = nullptr;
};
