/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioDeviceIdTests.h"

#include "audio/AudioDeviceId.h"

void AudioDeviceIdTests::roundTripsNonNegativeIds()
{
  for (const int id : {0, 1, 49, 2147483647}) {
    QCOMPARE(audioDeviceIdToInt(std::to_string(id)), id);
  }
}

void AudioDeviceIdTests::rejectsGarbage()
{
  QCOMPARE(audioDeviceIdToInt(""), 0);
  QCOMPARE(audioDeviceIdToInt("abc"), 0);
  QCOMPARE(audioDeviceIdToInt("12abc"), 0);
  QCOMPARE(audioDeviceIdToInt("12 "), 0);
}

QTEST_MAIN(AudioDeviceIdTests)
