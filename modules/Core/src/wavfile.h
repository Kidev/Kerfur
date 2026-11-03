// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_WAVFILE_H
#define KERFUR_WAVFILE_H

#include <QByteArray>

#include <vector>

namespace Kerfur::WavFile {

/// A RIFF WAVE file of 16 bit PCM, one channel, at `sampleRate`. Samples are
/// clamped to [-1, 1] before they are quantised.
QByteArray encodeMono16(const std::vector<float> &samples, int sampleRate);

/// Reads 8, 16, 24 or 32 bit integer PCM, or 32 bit float, at any rate and
/// channel count, and averages the channels down to one.
///
/// False for anything else, including a compressed WAVE, so that a caller can
/// fall back to a general decoder. This exists because a recording the
/// application wrote itself must read back on every platform, including the
/// ones where Qt Multimedia has no decoder.
bool decodeMono(const QByteArray &data, std::vector<float> &samples, int &sampleRate);

} // namespace Kerfur::WavFile

#endif // KERFUR_WAVFILE_H
