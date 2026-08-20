// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "filterbank.h"

#include <algorithm>
#include <cmath>

namespace Kerfur {

namespace {

constexpr int frameLengthMs{25};
constexpr int frameShiftMs{10};
constexpr float preemphasis{0.97F};

// Kaldi's mel band edges. The low cut removes rumble that carries no speaker
// information and plenty of room; the high edge is the Nyquist frequency.
constexpr double lowFrequency{20.0};

// torchaudio clamps the filterbank energies here before taking the log, and
// the value is load bearing rather than decorative: it is what the network saw
// wherever a band was silent.
constexpr float logFloor{1.1921e-07F};

// The model was trained on audio in the sixteen bit integer range rather than
// on normalized floats. The log is not scale invariant below the clamp above,
// so feeding it [-1, 1] audio floors most of the spectrum to a constant.
constexpr float integerScale{32768.0F};

double toMel(double frequency)
{
    return 1127.0 * std::log(1.0 + (frequency / 700.0));
}

int nextPowerOfTwo(int value)
{
    int size{1};
    while (size < value) {
        size *= 2;
    }
    return size;
}

} // namespace

FilterBank::FilterBank(int sampleRate, int melBins)
    : m_sampleRate{sampleRate > 0 ? sampleRate : 16000}
    , m_melBins{melBins > 0 ? melBins : 80}
    , m_frameLength{(m_sampleRate * frameLengthMs) / 1000}
    , m_frameShift{(m_sampleRate * frameShiftMs) / 1000}
    , m_fftSize{nextPowerOfTwo(m_frameLength)}
{
    // Symmetric Hamming, which is what torchaudio builds for window_type
    // "hamming" with periodic=False. The periodic variant divides by N instead
    // of N-1 and is a different window.
    m_window.resize(static_cast<size_t>(m_frameLength));
    for (int index{0}; index < m_frameLength; ++index) {
        m_window[static_cast<size_t>(index)] = static_cast<float>(
            0.54 - (0.46 * std::cos((2.0 * CV_PI * index) / (m_frameLength - 1))));
    }

    m_melMatrix = buildMelMatrix();
}

int FilterBank::frameCount(qsizetype sampleCount) const
{
    if (sampleCount < m_frameLength) {
        return 0;
    }
    return static_cast<int>(1 + ((sampleCount - m_frameLength) / m_frameShift));
}

cv::Mat FilterBank::buildMelMatrix() const
{
    const int spectrumBins{(m_fftSize / 2) + 1};
    cv::Mat matrix{cv::Mat::zeros(spectrumBins, m_melBins, CV_32F)};

    const double binWidth{static_cast<double>(m_sampleRate) / m_fftSize};
    const double highFrequency{m_sampleRate / 2.0};
    const double melLow{toMel(lowFrequency)};
    const double melHigh{toMel(highFrequency)};
    const double melDelta{(melHigh - melLow) / (m_melBins + 1)};

    // The triangles are laid over the first fftSize/2 bins only. The Nyquist
    // column stays zero, which is what torchaudio's trailing pad does, and
    // filling it instead adds a band the model has never seen.
    for (int bin{0}; bin < m_melBins; ++bin) {
        const double left{melLow + (bin * melDelta)};
        const double centre{left + melDelta};
        const double right{centre + melDelta};

        for (int index{0}; index < m_fftSize / 2; ++index) {
            const double mel{toMel(binWidth * index)};
            if (mel <= left || mel >= right) {
                continue;
            }
            const double weight{mel <= centre ? (mel - left) / (centre - left)
                                              : (right - mel) / (right - centre)};
            matrix.at<float>(index, bin) = static_cast<float>(weight);
        }
    }
    return matrix;
}

cv::Mat FilterBank::compute(const std::vector<float> &samples) const
{
    const int frames{frameCount(static_cast<qsizetype>(samples.size()))};
    if (frames <= 0) {
        return {};
    }

    // Every frame windowed into one matrix, so the transform is a single call
    // over its rows rather than one call per ten milliseconds of audio.
    cv::Mat windowed{cv::Mat::zeros(frames, m_fftSize, CV_32F)};
    std::vector<float> frame(static_cast<size_t>(m_frameLength));

    for (int index{0}; index < frames; ++index) {
        const float *source{samples.data() + (static_cast<size_t>(index) * m_frameShift)};

        double total{0.0};
        for (int position{0}; position < m_frameLength; ++position) {
            const float value{source[position] * integerScale};
            frame[static_cast<size_t>(position)] = value;
            total += value;
        }

        // Remove the offset, then pre-emphasise, then window, in that order.
        // Pre-emphasising before the offset is removed leaves the offset in as
        // a step at the start of every frame.
        const float mean{static_cast<float>(total / m_frameLength)};
        for (int position{0}; position < m_frameLength; ++position) {
            frame[static_cast<size_t>(position)] -= mean;
        }
        for (int position{m_frameLength - 1}; position > 0; --position) {
            frame[static_cast<size_t>(position)] -=
                preemphasis * frame[static_cast<size_t>(position - 1)];
        }
        frame[0] -= preemphasis * frame[0];

        float *destination{windowed.ptr<float>(index)};
        for (int position{0}; position < m_frameLength; ++position) {
            destination[position] = frame[static_cast<size_t>(position)]
                                    * m_window[static_cast<size_t>(position)];
        }
    }

    cv::Mat spectrum;
    cv::dft(windowed, spectrum, cv::DFT_ROWS | cv::DFT_COMPLEX_OUTPUT);

    const int spectrumBins{(m_fftSize / 2) + 1};
    cv::Mat power{cv::Mat::zeros(frames, spectrumBins, CV_32F)};
    for (int row{0}; row < frames; ++row) {
        const cv::Vec2f *complexRow{spectrum.ptr<cv::Vec2f>(row)};
        float *powerRow{power.ptr<float>(row)};
        for (int bin{0}; bin < spectrumBins; ++bin) {
            powerRow[bin] = (complexRow[bin][0] * complexRow[bin][0])
                            + (complexRow[bin][1] * complexRow[bin][1]);
        }
    }

    cv::Mat energies{power * m_melMatrix};
    cv::max(energies, logFloor, energies);
    cv::log(energies, energies);

    // Cepstral mean normalization over the utterance, which is the last thing
    // WeSpeaker does before the network. It is also what makes the integer
    // scaling above harmless: a constant factor on the audio is a constant
    // offset on every log bin, and this removes it.
    cv::Mat means;
    cv::reduce(energies, means, 0, cv::REDUCE_AVG, CV_32F);
    for (int row{0}; row < energies.rows; ++row) {
        energies.row(row) -= means;
    }
    return energies;
}

} // namespace Kerfur
