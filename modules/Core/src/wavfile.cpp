// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "wavfile.h"

#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Kerfur::WavFile {

namespace {

constexpr quint16 formatPcm{1};
constexpr quint16 formatFloat{3};
constexpr quint16 formatExtensible{0xFFFE};

void appendLittle16(QByteArray &out, quint16 value)
{
    std::array<char, 2> bytes{};
    qToLittleEndian(value, bytes.data());
    out.append(bytes.data(), static_cast<qsizetype>(bytes.size()));
}

void appendLittle32(QByteArray &out, quint32 value)
{
    std::array<char, 4> bytes{};
    qToLittleEndian(value, bytes.data());
    out.append(bytes.data(), static_cast<qsizetype>(bytes.size()));
}

quint16 little16(const char *data)
{
    return qFromLittleEndian<quint16>(data);
}

quint32 little32(const char *data)
{
    return qFromLittleEndian<quint32>(data);
}

/// One sample at `data`, `bytes` wide, scaled to [-1, 1].
float sampleAt(const char *data, int bytes, bool isFloat)
{
    if (isFloat) {
        float value{0.0F};
        std::memcpy(&value, data, sizeof(value));
        return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
    }
    switch (bytes) {
    case 1:
        return (static_cast<float>(static_cast<quint8>(data[0])) - 128.0F) / 128.0F;
    case 2:
        return static_cast<float>(qFromLittleEndian<qint16>(data)) / 32768.0F;
    case 3: {
        // Sign extended by shifting into the top of a 32 bit word and back.
        const quint32 raw{static_cast<quint32>(static_cast<quint8>(data[0]))
                          | (static_cast<quint32>(static_cast<quint8>(data[1])) << 8U)
                          | (static_cast<quint32>(static_cast<quint8>(data[2])) << 16U)};
        const qint32 value{static_cast<qint32>(raw << 8U) / 256};
        return static_cast<float>(value) / 8388608.0F;
    }
    case 4:
        return static_cast<float>(static_cast<double>(qFromLittleEndian<qint32>(data))
                                  / 2147483648.0);
    default:
        return 0.0F;
    }
}

} // namespace

QByteArray encodeMono16(const std::vector<float> &samples, int sampleRate)
{
    const auto dataBytes{static_cast<quint32>(samples.size() * sizeof(qint16))};

    QByteArray out;
    out.reserve(static_cast<qsizetype>(44 + dataBytes));
    out.append("RIFF", 4);
    appendLittle32(out, 36 + dataBytes);
    out.append("WAVE", 4);

    out.append("fmt ", 4);
    appendLittle32(out, 16);
    appendLittle16(out, formatPcm);
    appendLittle16(out, 1);
    appendLittle32(out, static_cast<quint32>(sampleRate));
    appendLittle32(out, static_cast<quint32>(sampleRate) * 2U);
    appendLittle16(out, 2);
    appendLittle16(out, 16);

    out.append("data", 4);
    appendLittle32(out, dataBytes);
    for (const float sample : samples) {
        const float clamped{std::isfinite(sample) ? std::clamp(sample, -1.0F, 1.0F) : 0.0F};
        const auto quantised{static_cast<qint16>(std::lround(clamped * 32767.0F))};
        appendLittle16(out, static_cast<quint16>(quantised));
    }
    return out;
}

bool decodeMono(const QByteArray &data, std::vector<float> &samples, int &sampleRate)
{
    samples.clear();
    sampleRate = 0;

    if (data.size() < 12 || std::memcmp(data.constData(), "RIFF", 4) != 0
        || std::memcmp(data.constData() + 8, "WAVE", 4) != 0) {
        return false;
    }

    quint16 format{0};
    int channels{0};
    int bitsPerSample{0};
    int blockAlign{0};
    bool haveFormat{false};

    // Chunks are walked rather than assumed at fixed offsets: a recorder is
    // free to put LIST, fact or bext chunks anywhere before the samples.
    qsizetype offset{12};
    while (offset + 8 <= data.size()) {
        const char *header{data.constData() + offset};
        const quint32 chunkSize{little32(header + 4)};
        const qsizetype bodyStart{offset + 8};
        const qsizetype available{data.size() - bodyStart};
        const qsizetype bodySize{
            std::min(static_cast<qsizetype>(chunkSize), std::max<qsizetype>(available, 0))};
        const char *body{data.constData() + bodyStart};

        if (std::memcmp(header, "fmt ", 4) == 0) {
            if (bodySize < 16) {
                return false;
            }
            format = little16(body);
            channels = little16(body + 2);
            sampleRate = static_cast<int>(little32(body + 4));
            blockAlign = little16(body + 12);
            bitsPerSample = little16(body + 14);
            if (format == formatExtensible && bodySize >= 26) {
                // The real format is the first two bytes of the sub format GUID.
                format = little16(body + 24);
            }
            haveFormat = true;
        } else if (std::memcmp(header, "data", 4) == 0) {
            if (!haveFormat) {
                return false;
            }
            const bool isFloat{format == formatFloat};
            if ((format != formatPcm && !isFloat) || channels <= 0 || sampleRate <= 0
                || (isFloat && bitsPerSample != 32)) {
                return false;
            }
            const int bytes{bitsPerSample / 8};
            if (bytes < 1 || bytes > 4 || bitsPerSample % 8 != 0
                || blockAlign != bytes * channels) {
                return false;
            }

            const qsizetype frames{bodySize / blockAlign};
            samples.reserve(static_cast<size_t>(frames));
            for (qsizetype frame{0}; frame < frames; ++frame) {
                const char *at{body + (frame * blockAlign)};
                float total{0.0F};
                for (int channel{0}; channel < channels; ++channel) {
                    total += sampleAt(at + (channel * bytes), bytes, isFloat);
                }
                samples.push_back(total / static_cast<float>(channels));
            }
            return true;
        }

        // Chunks are padded to an even length.
        offset = bodyStart + static_cast<qsizetype>(chunkSize) + (chunkSize & 1U);
    }

    samples.clear();
    sampleRate = 0;
    return false;
}

} // namespace Kerfur::WavFile
