// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "audiofile.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QEventLoop>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(audioFileLog, "kerfur.hearing.audiofile")

// A gallery recording that has not finished decoding by now is not going to.
// Without this a malformed file would hold the nested loop, and the nested loop
// is on the path that starts the application.
constexpr int decodeTimeoutMs{30000};

float sampleAt(const QAudioBuffer &buffer, qsizetype index)
{
    switch (buffer.format().sampleFormat()) {
    case QAudioFormat::UInt8:
        return (buffer.constData<quint8>()[index] - 128) / 128.0F;
    case QAudioFormat::Int16:
        return buffer.constData<qint16>()[index] / 32768.0F;
    case QAudioFormat::Int32:
        return static_cast<float>(buffer.constData<qint32>()[index] / 2147483648.0);
    case QAudioFormat::Float:
        return buffer.constData<float>()[index];
    default:
        return 0.0F;
    }
}

void appendMono(const QAudioBuffer &buffer, std::vector<float> &out)
{
    const int channels{std::max(1, buffer.format().channelCount())};
    const qsizetype frames{buffer.frameCount()};
    out.reserve(out.size() + static_cast<size_t>(frames));

    for (qsizetype frame{0}; frame < frames; ++frame) {
        float total{0.0F};
        for (int channel{0}; channel < channels; ++channel) {
            total += sampleAt(buffer, (frame * channels) + channel);
        }
        out.push_back(total / channels);
    }
}

std::vector<float> resample(const std::vector<float> &input, int from, int to)
{
    if (from == to || input.empty() || from <= 0) {
        return input;
    }

    const double ratio{static_cast<double>(to) / from};
    const auto count{static_cast<qsizetype>(input.size() * ratio)};
    std::vector<float> output;
    output.reserve(static_cast<size_t>(count));
    for (qsizetype index{0}; index < count; ++index) {
        const double position{index / ratio};
        const auto lower{static_cast<qsizetype>(position)};
        const double fraction{position - lower};
        if (lower + 1 < static_cast<qsizetype>(input.size())) {
            output.push_back(
                static_cast<float>((input[lower] * (1.0 - fraction)) + (input[lower + 1] * fraction)));
        } else if (lower < static_cast<qsizetype>(input.size())) {
            output.push_back(input[lower]);
        }
    }
    return output;
}

} // namespace

std::vector<float> decodeMonoAudio(const QString &path, int sampleRate, QString *error)
{
    const auto fail = [error](const QString &reason) {
        if (error != nullptr) {
            *error = reason;
        }
        return std::vector<float>{};
    };

    if (!QFileInfo::exists(path)) {
        return fail(QStringLiteral("no such file"));
    }

    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(path));

    // Deliberately no setAudioFormat. Qt 6.11's FFmpeg backend accepts a
    // requested output format, reports that it is decoding, and then produces
    // no buffers at all and never finishes; measured against this same file,
    // asking for 16 kHz mono float yielded zero buffers where asking for
    // nothing yielded the whole recording. The conversion is done below
    // instead, which is also the only path that works when a backend honours
    // the request partially.

    std::vector<float> samples;
    int sourceRate{0};
    QString failure;

    QEventLoop loop;
    QObject::connect(&decoder,
                     &QAudioDecoder::bufferReady,
                     &decoder,
                     [&decoder, &samples, &sourceRate]() {
                         // Drained rather than read once: one signal can cover
                         // several buffers, and a backend whose buffers are not
                         // taken stops producing them.
                         while (decoder.bufferAvailable()) {
                             const QAudioBuffer buffer{decoder.read()};
                             if (!buffer.isValid()) {
                                 break;
                             }
                             if (sourceRate == 0) {
                                 sourceRate = buffer.format().sampleRate();
                             }
                             appendMono(buffer, samples);
                         }
                     });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&decoder,
                     qOverload<QAudioDecoder::Error>(&QAudioDecoder::error),
                     &loop,
                     [&loop, &failure, &decoder](QAudioDecoder::Error) {
                         failure = decoder.errorString();
                         loop.quit();
                     });

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, [&loop, &failure]() {
        failure = QStringLiteral("decoding timed out");
        loop.quit();
    });
    guard.start(decodeTimeoutMs);

    decoder.start();
    loop.exec();
    decoder.stop();

    if (!failure.isEmpty()) {
        return fail(failure);
    }
    if (samples.empty() || sourceRate <= 0) {
        return fail(QStringLiteral("decoded to nothing"));
    }

    qCDebug(audioFileLog) << path << "decoded" << samples.size() << "frames at" << sourceRate;
    return resample(samples, sourceRate, sampleRate);
}

} // namespace Kerfur
