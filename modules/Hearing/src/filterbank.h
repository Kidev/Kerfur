// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_FILTERBANK_H
#define KERFUR_FILTERBANK_H

#include <QtGlobal>

#include <opencv2/core.hpp>

#include <vector>

namespace Kerfur {

/// The log mel filterbank the speaker model was trained on.
///
/// This is a reimplementation of Kaldi's `compute-fbank-feats` as exposed by
/// `torchaudio.compliance.kaldi.fbank`, because that is literally what
/// WeSpeaker calls before its network and a speaker embedding is only
/// comparable to the gallery if both were computed the same way. Every constant
/// here is one of that function's defaults, and none of them is a free choice:
/// changing the window, the pre-emphasis or the mel formula does not degrade
/// the embedding, it produces a different one that matches nothing.
class FilterBank
{
public:
    explicit FilterBank(int sampleRate = 16000, int melBins = 80);

    /// `samples` are mono in the usual [-1, 1] range. Returns a
    /// `frames x melBins` CV_32F matrix with the mean over time removed from
    /// each bin, which is the cepstral mean normalization the model expects,
    /// or an empty matrix when there is not a whole frame of audio.
    cv::Mat compute(const std::vector<float> &samples) const;

    int melBins() const { return m_melBins; }
    int frameShiftSamples() const { return m_frameShift; }
    int frameLengthSamples() const { return m_frameLength; }

    /// How many frames `sampleCount` yields. Kaldi snips the edges rather than
    /// padding them, so a signal shorter than one window yields nothing at all.
    int frameCount(qsizetype sampleCount) const;

private:
    cv::Mat buildMelMatrix() const;

    int m_sampleRate;
    int m_melBins;
    int m_frameLength;
    int m_frameShift;
    int m_fftSize;
    std::vector<float> m_window;
    cv::Mat m_melMatrix;
};

} // namespace Kerfur

#endif // KERFUR_FILTERBANK_H
