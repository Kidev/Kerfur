// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "voiceactivity.h"

#include <QTest>

#include <cmath>
#include <vector>

using namespace Kerfur;

namespace {

constexpr int rate{16000};

void appendSilence(std::vector<float> &stream, int milliseconds)
{
    stream.insert(stream.end(), static_cast<size_t>((rate * milliseconds) / 1000), 0.0F);
}

/// A tone rather than noise, because the gate measures energy and a tone is the
/// simplest thing with a predictable amount of it.
void appendTone(std::vector<float> &stream, int milliseconds, float amplitude = 0.25F)
{
    const int count{(rate * milliseconds) / 1000};
    for (int index{0}; index < count; ++index) {
        stream.push_back(amplitude * std::sin(2.0F * 3.14159265358979323846F * 220.0F
                                              * index / rate));
    }
}

std::vector<VoiceActivitySegmenter::Utterance> run(VoiceActivitySegmenter &segmenter,
                                                   const std::vector<float> &stream)
{
    std::vector<VoiceActivitySegmenter::Utterance> all;
    // Fed in the lumps a live capture arrives in, so that the frame assembly is
    // exercised rather than bypassed by one large call.
    constexpr qsizetype chunk{320};
    for (qsizetype offset{0}; offset < static_cast<qsizetype>(stream.size()); offset += chunk) {
        const qsizetype take{
            std::min(chunk, static_cast<qsizetype>(stream.size()) - offset)};
        for (auto &utterance : segmenter.append(stream.data() + offset, take)) {
            all.push_back(std::move(utterance));
        }
    }
    return all;
}

} // namespace

class TestVoiceActivity : public QObject
{
    Q_OBJECT

private slots:
    void silenceProducesNothing()
    {
        VoiceActivitySegmenter segmenter{rate};
        std::vector<float> stream;
        appendSilence(stream, 3000);
        QVERIFY(run(segmenter, stream).empty());
        QVERIFY(!segmenter.inSpeech());
    }

    void oneBurstBecomesOneUtterance()
    {
        VoiceActivitySegmenter segmenter{rate};
        segmenter.setSilenceTailMs(400);

        std::vector<float> stream;
        appendSilence(stream, 500);
        appendTone(stream, 1200);
        appendSilence(stream, 1000);

        const auto utterances{run(segmenter, stream)};
        QCOMPARE(utterances.size(), size_t{1});

        // The pre-roll means the utterance carries more audio than the speech
        // it contains, which is the point of it: the first phoneme is spoken
        // before the gate has decided anything is happening.
        QVERIFY(utterances[0].samples.size() >= size_t{rate * 1200 / 1000});
        QVERIFY(std::abs(utterances[0].durationMs - 1200) < 250);
    }

    /// A gap shorter than the tail is a pause inside a sentence, not the end of
    /// one. Cutting there would hand Whisper half a clause.
    void aShortGapDoesNotEndAnUtterance()
    {
        VoiceActivitySegmenter segmenter{rate};
        segmenter.setSilenceTailMs(600);

        std::vector<float> stream;
        appendSilence(stream, 400);
        appendTone(stream, 700);
        appendSilence(stream, 300);
        appendTone(stream, 700);
        appendSilence(stream, 1200);

        QCOMPARE(run(segmenter, stream).size(), size_t{1});
    }

    void aLongGapSeparatesTwoUtterances()
    {
        VoiceActivitySegmenter segmenter{rate};
        segmenter.setSilenceTailMs(400);

        std::vector<float> stream;
        appendSilence(stream, 300);
        appendTone(stream, 700);
        appendSilence(stream, 1500);
        appendTone(stream, 700);
        appendSilence(stream, 1000);

        const auto utterances{run(segmenter, stream)};
        QCOMPARE(utterances.size(), size_t{2});
        QVERIFY(utterances[1].startMs > utterances[0].startMs);
    }

    /// A door or a keyboard is loud and brief, and Whisper will confidently
    /// invent a sentence for it.
    void aClickIsTooShortToBeSpeech()
    {
        VoiceActivitySegmenter segmenter{rate};
        segmenter.setSilenceTailMs(400);

        std::vector<float> stream;
        appendSilence(stream, 300);
        appendTone(stream, 90);
        appendSilence(stream, 1000);

        QVERIFY(run(segmenter, stream).empty());
    }

    /// Somebody reading aloud never pauses. Without a cut the recogniser is
    /// never handed anything at all.
    void endlessSpeechIsCutAnyway()
    {
        VoiceActivitySegmenter segmenter{rate};
        segmenter.setMaximumUtteranceMs(2000);

        std::vector<float> stream;
        appendSilence(stream, 200);
        appendTone(stream, 9000);

        const auto utterances{run(segmenter, stream)};
        QVERIFY(utterances.size() >= size_t{3});
        QVERIFY(segmenter.inSpeech());
    }

    void flushEndsSpeechInProgress()
    {
        VoiceActivitySegmenter segmenter{rate};
        std::vector<float> stream;
        appendSilence(stream, 200);
        appendTone(stream, 1000);

        QVERIFY(run(segmenter, stream).empty());
        QVERIFY(segmenter.inSpeech());

        const auto flushed{segmenter.flush()};
        QVERIFY(flushed.has_value());
        QVERIFY(!segmenter.inSpeech());
        QVERIFY(!segmenter.flush().has_value());
    }

    /// The threshold follows the room. A constant hiss well below speech must
    /// not open an utterance, and after it the gate must still hear speech.
    void steadyNoiseDoesNotOpenAnUtterance()
    {
        VoiceActivitySegmenter segmenter{rate};
        segmenter.setSilenceTailMs(400);

        std::vector<float> stream;
        appendTone(stream, 3000, 0.002F);
        QVERIFY(run(segmenter, stream).empty());

        std::vector<float> speech;
        appendTone(speech, 900, 0.25F);
        appendSilence(speech, 1000);
        QCOMPARE(run(segmenter, speech).size(), size_t{1});
    }

    void resetForgetsEverything()
    {
        VoiceActivitySegmenter segmenter{rate};
        std::vector<float> stream;
        appendSilence(stream, 200);
        appendTone(stream, 1000);
        run(segmenter, stream);
        QVERIFY(segmenter.inSpeech());

        segmenter.reset();
        QVERIFY(!segmenter.inSpeech());
        QVERIFY(!segmenter.flush().has_value());
    }
};

QTEST_MAIN(TestVoiceActivity)
#include "tst_voiceactivity.moc"
