// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "wavfile.h"

#include <QTest>
#include <QtEndian>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>

using namespace Kerfur;

namespace {

void append16(QByteArray &out, quint16 value)
{
    std::array<char, 2> bytes{};
    qToLittleEndian(value, bytes.data());
    out.append(bytes.data(), 2);
}

void append32(QByteArray &out, quint32 value)
{
    std::array<char, 4> bytes{};
    qToLittleEndian(value, bytes.data());
    out.append(bytes.data(), 4);
}

/// A canonical header for `format`, followed by `payload` as the data chunk.
/// `extra` goes between the two, the way a recorder's LIST chunk does.
QByteArray waveFile(quint16 format,
                    quint16 channels,
                    quint32 rate,
                    quint16 bits,
                    const QByteArray &payload,
                    const QByteArray &extra = {})
{
    QByteArray out{"RIFF"};
    append32(out, static_cast<quint32>(4 + 24 + extra.size() + 8 + payload.size()));
    out.append("WAVE");
    out.append("fmt ");
    append32(out, 16);
    append16(out, format);
    append16(out, channels);
    append32(out, rate);
    append32(out, rate * channels * (bits / 8U));
    append16(out, static_cast<quint16>(channels * (bits / 8U)));
    append16(out, bits);
    out.append(extra);
    out.append("data");
    append32(out, static_cast<quint32>(payload.size()));
    out.append(payload);
    return out;
}

} // namespace

class TestWavFile : public QObject
{
    Q_OBJECT

private slots:
    void aRecordingRoundTrips()
    {
        std::vector<float> tone;
        for (int index{0}; index < 1600; ++index) {
            tone.push_back(static_cast<float>(
                0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * index / 16000.0)));
        }

        std::vector<float> decoded;
        int rate{0};
        QVERIFY(WavFile::decodeMono(WavFile::encodeMono16(tone, 16000), decoded, rate));
        QCOMPARE(rate, 16000);
        QCOMPARE(decoded.size(), tone.size());
        for (size_t index{0}; index < tone.size(); ++index) {
            QVERIFY(std::abs(decoded[index] - tone[index]) < 1.0F / 16000.0F);
        }
    }

    void anOutOfRangeSampleIsClamped()
    {
        std::vector<float> decoded;
        int rate{0};
        const std::vector<float> wild{4.0F, -4.0F, std::numeric_limits<float>::quiet_NaN()};
        QVERIFY(WavFile::decodeMono(WavFile::encodeMono16(wild, 8000), decoded, rate));
        QCOMPARE(decoded.size(), size_t{3});
        QVERIFY(decoded[0] > 0.99F);
        QVERIFY(decoded[1] < -0.99F);
        QCOMPARE(decoded[2], 0.0F);
    }

    void stereoIsAveragedToMono()
    {
        QByteArray payload;
        append16(payload, static_cast<quint16>(static_cast<qint16>(16384)));
        append16(payload, static_cast<quint16>(static_cast<qint16>(-16384)));
        append16(payload, static_cast<quint16>(static_cast<qint16>(16384)));
        append16(payload, static_cast<quint16>(static_cast<qint16>(16384)));

        std::vector<float> decoded;
        int rate{0};
        QVERIFY(WavFile::decodeMono(waveFile(1, 2, 44100, 16, payload), decoded, rate));
        QCOMPARE(rate, 44100);
        QCOMPARE(decoded.size(), size_t{2});
        QCOMPARE(decoded[0], 0.0F);
        QCOMPARE(decoded[1], 0.5F);
    }

    void aChunkBeforeTheSamplesIsSkipped()
    {
        QByteArray list{"LIST"};
        append32(list, 3);
        list.append("abc");
        list.append('\0'); // padding to an even length

        QByteArray payload;
        append16(payload, static_cast<quint16>(static_cast<qint16>(-32768)));

        std::vector<float> decoded;
        int rate{0};
        QVERIFY(WavFile::decodeMono(waveFile(1, 1, 16000, 16, payload, list), decoded, rate));
        QCOMPARE(decoded.size(), size_t{1});
        QCOMPARE(decoded[0], -1.0F);
    }

    void twentyFourBitIsSignExtended()
    {
        // -2^22 and +2^22, little endian, three bytes each.
        const QByteArray payload{QByteArrayLiteral("\x00\x00\xC0\x00\x00\x40")};

        std::vector<float> decoded;
        int rate{0};
        QVERIFY(WavFile::decodeMono(waveFile(1, 1, 16000, 24, payload), decoded, rate));
        QCOMPARE(decoded.size(), size_t{2});
        QCOMPARE(decoded[0], -0.5F);
        QCOMPARE(decoded[1], 0.5F);
    }

    void aCompressedWaveIsRefused()
    {
        // Format 2 is Microsoft ADPCM, which this reader does not decode and
        // must not pretend to.
        std::vector<float> decoded;
        int rate{0};
        QVERIFY(!WavFile::decodeMono(waveFile(2, 1, 16000, 4, QByteArray(16, '\0')),
                                     decoded,
                                     rate));
        QVERIFY(decoded.empty());
    }

    void somethingElseIsRefused()
    {
        std::vector<float> decoded;
        int rate{0};
        QVERIFY(!WavFile::decodeMono(QByteArrayLiteral("ID3\x04 not a wave at all"),
                                     decoded,
                                     rate));
        QVERIFY(!WavFile::decodeMono({}, decoded, rate));
    }
};

QTEST_GUILESS_MAIN(TestWavFile)

#include "tst_wavfile.moc"
