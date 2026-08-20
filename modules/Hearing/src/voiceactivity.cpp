// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "voiceactivity.h"

#include <algorithm>
#include <cmath>

namespace Kerfur {

namespace {

// Audio kept from before speech was declared. The gate needs a few frames to
// be sure, and those frames are the beginning of the first word.
constexpr int prerollMs{300};

// How far above the noise floor a frame must be. Speech is well over ten
// decibels above room noise; three is chosen low enough to catch a quiet
// speaker and high enough that a fan does not open an utterance every second.
constexpr float speechRatio{3.0F};

// The floor below which the gate stops trusting the adaptive threshold. In a
// silent room the measured floor tends towards zero, and three times almost
// nothing is still almost nothing.
constexpr float absoluteThreshold{0.0055F};

// How fast the floor follows the room. Down quickly, so that the gate recovers
// as soon as a noise stops; up slowly, so that a spoken sentence does not raise
// the floor above itself and cut its own ending off.
constexpr float floorFallRate{0.12F};
constexpr float floorRiseRate{0.0015F};

// Anything shorter than this is a door, a keyboard, or a cough. Whisper will
// happily invent a sentence for it.
constexpr int minimumUtteranceMs{220};

float frameRms(const float *samples, int count)
{
    if (count <= 0) {
        return 0.0F;
    }
    double total{0.0};
    for (int index{0}; index < count; ++index) {
        total += static_cast<double>(samples[index]) * samples[index];
    }
    return static_cast<float>(std::sqrt(total / count));
}

} // namespace

VoiceActivitySegmenter::VoiceActivitySegmenter(int sampleRate)
    : m_sampleRate{sampleRate > 0 ? sampleRate : 16000}
    , m_frameSamples{(m_sampleRate * frameMs) / 1000}
    , m_prerollSamples{(m_sampleRate * prerollMs) / 1000}
    , m_maximumSamples{m_sampleRate * 12}
    , m_minimumSamples{(m_sampleRate * minimumUtteranceMs) / 1000}
{
    m_partialFrame.reserve(static_cast<size_t>(m_frameSamples));
    m_pending.reserve(static_cast<size_t>(m_prerollSamples) * 2);
}

void VoiceActivitySegmenter::setSilenceTailMs(int milliseconds)
{
    m_tailFrames = std::max(1, milliseconds / frameMs);
}

void VoiceActivitySegmenter::setMaximumUtteranceMs(int milliseconds)
{
    m_maximumSamples = std::max(m_frameSamples, (m_sampleRate * milliseconds) / 1000);
}

float VoiceActivitySegmenter::speechThreshold() const
{
    return std::max(m_noiseFloor * speechRatio, absoluteThreshold);
}

void VoiceActivitySegmenter::reset()
{
    m_state = State::Silence;
    m_aboveFrames = 0;
    m_belowFrames = 0;
    m_pending.clear();
    m_partialFrame.clear();
    m_speechOffset = 0;
    m_streamSamples = 0;
    m_utteranceStartSample = 0;
    m_noiseFloor = 0.003F;
}

void VoiceActivitySegmenter::trimPreroll()
{
    const qsizetype excess{static_cast<qsizetype>(m_pending.size()) - m_prerollSamples};
    if (excess > 0) {
        m_pending.erase(m_pending.begin(), m_pending.begin() + excess);
    }
}

std::optional<VoiceActivitySegmenter::Utterance> VoiceActivitySegmenter::finishUtterance()
{
    // The tail of silence that ended the utterance is not part of it, but a
    // little of it is: Whisper decodes the last word better with some room
    // after it than with the recording stopping on the consonant.
    const qsizetype keepTail{static_cast<qsizetype>(m_frameSamples) * 5};
    const qsizetype tail{static_cast<qsizetype>(m_belowFrames) * m_frameSamples};
    qsizetype end{static_cast<qsizetype>(m_pending.size()) - std::max<qsizetype>(0, tail - keepTail)};
    end = std::clamp<qsizetype>(end, m_speechOffset, static_cast<qsizetype>(m_pending.size()));

    Utterance utterance;
    utterance.samples.assign(m_pending.begin(), m_pending.begin() + end);
    utterance.startMs = (m_utteranceStartSample * 1000) / m_sampleRate;

    const qsizetype spoken{end - m_speechOffset};
    utterance.durationMs = (static_cast<qint64>(spoken) * 1000) / m_sampleRate;

    m_state = State::Silence;
    m_aboveFrames = 0;
    m_belowFrames = 0;
    m_speechOffset = 0;

    // Only the pre-roll survives into the next utterance, and it has to come
    // from after the one just emitted or the next transcript starts with the
    // end of this one.
    m_pending.erase(m_pending.begin(), m_pending.begin() + end);
    trimPreroll();

    if (spoken < m_minimumSamples) {
        return std::nullopt;
    }
    return utterance;
}

std::vector<VoiceActivitySegmenter::Utterance> VoiceActivitySegmenter::append(const float *samples,
                                                                             qsizetype count)
{
    std::vector<Utterance> finished;
    if (samples == nullptr || count <= 0) {
        return finished;
    }

    qsizetype consumed{0};
    while (consumed < count) {
        const qsizetype wanted{m_frameSamples - static_cast<qsizetype>(m_partialFrame.size())};
        const qsizetype take{std::min(wanted, count - consumed)};
        m_partialFrame.insert(m_partialFrame.end(),
                              samples + consumed,
                              samples + consumed + take);
        consumed += take;

        if (static_cast<qsizetype>(m_partialFrame.size()) < m_frameSamples) {
            break;
        }

        const float rms{frameRms(m_partialFrame.data(), m_frameSamples)};
        const float threshold{speechThreshold()};
        const bool loud{rms > threshold};

        m_pending.insert(m_pending.end(), m_partialFrame.cbegin(), m_partialFrame.cend());
        m_partialFrame.clear();
        m_streamSamples += m_frameSamples;

        // The floor only learns from frames the gate is not calling speech.
        // Letting it learn from speech is what makes a long sentence raise the
        // threshold above itself and cut off its own ending.
        if (!loud || m_state == State::Silence) {
            const float rate{rms < m_noiseFloor ? floorFallRate : floorRiseRate};
            m_noiseFloor += (rms - m_noiseFloor) * rate;
            m_noiseFloor = std::max(m_noiseFloor, 1e-5F);
        }

        if (m_state == State::Silence) {
            if (loud) {
                ++m_aboveFrames;
                if (m_aboveFrames >= m_onsetFrames) {
                    m_state = State::Speech;
                    m_belowFrames = 0;

                    // Speech began `m_onsetFrames` ago, and everything before
                    // that in `m_pending` is the pre-roll.
                    const qsizetype onset{static_cast<qsizetype>(m_aboveFrames) * m_frameSamples};
                    m_speechOffset =
                        std::max<qsizetype>(0,
                                            static_cast<qsizetype>(m_pending.size()) - onset);
                    m_utteranceStartSample = m_streamSamples - onset;
                    m_aboveFrames = 0;
                }
            } else {
                m_aboveFrames = 0;
                trimPreroll();
            }
            continue;
        }

        if (loud) {
            m_belowFrames = 0;
        } else {
            ++m_belowFrames;
            if (m_belowFrames >= m_tailFrames) {
                if (std::optional<Utterance> utterance{finishUtterance()}) {
                    finished.push_back(std::move(*utterance));
                }
                continue;
            }
        }

        if (static_cast<qsizetype>(m_pending.size()) - m_speechOffset >= m_maximumSamples) {
            // Cut, but stay in speech: somebody is still talking, and the next
            // utterance starts where this one was cut rather than after a
            // silence that never happened.
            Utterance utterance;
            utterance.samples.assign(m_pending.begin(), m_pending.end());
            utterance.startMs = (m_utteranceStartSample * 1000) / m_sampleRate;
            utterance.durationMs = (static_cast<qint64>(m_pending.size() - m_speechOffset) * 1000)
                                   / m_sampleRate;
            finished.push_back(std::move(utterance));

            m_pending.clear();
            m_speechOffset = 0;
            m_utteranceStartSample = m_streamSamples;
            m_belowFrames = 0;
        }
    }

    return finished;
}

std::optional<VoiceActivitySegmenter::Utterance> VoiceActivitySegmenter::flush()
{
    if (m_state != State::Speech) {
        m_partialFrame.clear();
        return std::nullopt;
    }
    if (!m_partialFrame.empty()) {
        m_pending.insert(m_pending.end(), m_partialFrame.cbegin(), m_partialFrame.cend());
        m_streamSamples += static_cast<qint64>(m_partialFrame.size());
        m_partialFrame.clear();
    }
    m_belowFrames = 0;
    return finishUtterance();
}

} // namespace Kerfur
