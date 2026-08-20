// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_VOICEACTIVITY_H
#define KERFUR_VOICEACTIVITY_H

#include <QtGlobal>

#include <optional>
#include <vector>

namespace Kerfur {

/// Cuts a continuous microphone stream into utterances.
///
/// This exists because the thing downstream is Whisper, and Whisper is not a
/// streaming recogniser. Handing it a fixed window every second, as this did
/// before, means handing it the same speech several times over: the same words
/// come back on every pass, the text never changes, and a display that only
/// updates when the text changes shows the first thing it ever heard for ever.
/// A recogniser that is given whole utterances, once each, does not have that
/// failure mode at all.
///
/// The gate is energy against an adapting noise floor rather than a fixed
/// level, because the level that means "someone is talking" in a quiet room is
/// the level that means "the fan is on" in a loud one.
class VoiceActivitySegmenter
{
public:
    struct Utterance
    {
        std::vector<float> samples;
        /// Milliseconds of stream time at which the speech started, counting
        /// from the first sample ever appended.
        qint64 startMs{0};
        qint64 durationMs{0};
    };

    explicit VoiceActivitySegmenter(int sampleRate = 16000);

    /// Feeds samples and returns every utterance that finished inside them.
    /// Usually empty; occasionally one; more than one only when a long backlog
    /// arrives at once.
    std::vector<Utterance> append(const float *samples, qsizetype count);

    /// Ends whatever is in progress, for a microphone that is being stopped.
    std::optional<Utterance> flush();

    void reset();

    bool inSpeech() const { return m_state == State::Speech; }

    /// The level the gate is currently measuring against, as an rms amplitude.
    /// Exposed because a microphone that hears nothing and a room that is
    /// simply loud look identical from outside.
    float noiseFloor() const { return m_noiseFloor; }
    float speechThreshold() const;

    /// Consecutive frames above the threshold before speech is declared. Short,
    /// because the pre-roll means nothing is lost by declaring late.
    void setOnsetFrames(int frames) { m_onsetFrames = frames; }

    /// Silence that ends an utterance. Long enough to sit through the pause in
    /// the middle of a sentence, short enough that a reply does not feel
    /// delayed.
    void setSilenceTailMs(int milliseconds);

    /// Speech longer than this is cut anyway. Somebody reading aloud never
    /// stops, and a recogniser that is never given anything transcribes
    /// nothing.
    void setMaximumUtteranceMs(int milliseconds);

    static constexpr int frameMs{20};

private:
    enum class State { Silence, Speech };

    std::optional<Utterance> finishUtterance();
    void trimPreroll();

    int m_sampleRate;
    int m_frameSamples;
    int m_prerollSamples;
    int m_onsetFrames{3};
    int m_tailFrames{30};
    int m_maximumSamples;
    int m_minimumSamples;

    State m_state{State::Silence};
    int m_aboveFrames{0};
    int m_belowFrames{0};

    /// Audio kept for the utterance being built, including the pre-roll that
    /// precedes it. In silence it holds only the pre-roll.
    std::vector<float> m_pending;
    std::vector<float> m_partialFrame;

    /// Where in `m_pending` the speech itself begins, so that the pre-roll can
    /// be included in the utterance and excluded from its reported start.
    qsizetype m_speechOffset{0};

    qint64 m_streamSamples{0};
    qint64 m_utteranceStartSample{0};

    float m_noiseFloor{0.003F};
};

} // namespace Kerfur

#endif // KERFUR_VOICEACTIVITY_H
