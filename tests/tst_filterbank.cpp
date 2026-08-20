// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "filterbank.h"

#include <QTest>

#include <cmath>
#include <vector>

using namespace Kerfur;

namespace {

constexpr int rate{16000};

std::vector<float> tone(double frequency, int milliseconds, float amplitude = 0.3F)
{
    const int count{(rate * milliseconds) / 1000};
    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(count));
    for (int index{0}; index < count; ++index) {
        samples.push_back(
            amplitude * static_cast<float>(std::sin(2.0 * M_PI * frequency * index / rate)));
    }
    return samples;
}

} // namespace

class TestFilterBank : public QObject
{
    Q_OBJECT

private slots:
    /// Kaldi snips the edges rather than padding them, so the frame count is
    /// exactly one plus however many whole shifts fit after the first window.
    void frameCountFollowsKaldi()
    {
        const FilterBank bank{rate, 80};
        QCOMPARE(bank.frameLengthSamples(), 400);
        QCOMPARE(bank.frameShiftSamples(), 160);
        QCOMPARE(bank.frameCount(399), 0);
        QCOMPARE(bank.frameCount(400), 1);
        QCOMPARE(bank.frameCount(560), 2);
        QCOMPARE(bank.frameCount(rate), 98);
    }

    void tooShortProducesNothing()
    {
        const FilterBank bank{rate, 80};
        QVERIFY(bank.compute(tone(440.0, 10)).empty());
    }

    void shapeMatchesTheRequest()
    {
        const FilterBank bank{rate, 80};
        const cv::Mat features{bank.compute(tone(440.0, 1000))};
        QCOMPARE(features.cols, 80);
        QCOMPARE(features.rows, bank.frameCount(rate));
        QCOMPARE(features.type(), CV_32F);
    }

    /// The mean over time is removed from every bin, which is the cepstral mean
    /// normalization the speaker model was trained behind. Getting the axis
    /// wrong here would normalize each frame instead and produce features that
    /// are not wrong so much as unrelated.
    void everyBandIsCentredOverTime()
    {
        const FilterBank bank{rate, 80};
        const cv::Mat features{bank.compute(tone(300.0, 1500))};
        QVERIFY(features.rows > 10);

        for (int column{0}; column < features.cols; ++column) {
            double total{0.0};
            for (int row{0}; row < features.rows; ++row) {
                total += features.at<float>(row, column);
            }
            QVERIFY2(std::abs(total / features.rows) < 1e-3,
                     qPrintable(QStringLiteral("band %1 mean %2")
                                    .arg(column)
                                    .arg(total / features.rows)));
        }
    }

    /// The bands are ordered by frequency, so a low tone must put its energy
    /// low. A reversed mel scale still produces plausible looking numbers.
    void aToneLandsInTheBandItBelongsTo()
    {
        const FilterBank bank{rate, 80};

        const auto peakBand = [&bank](double frequency) {
            const cv::Mat features{bank.compute(tone(frequency, 1000))};
            const cv::Mat row{features.row(features.rows / 2)};
            int best{0};
            for (int column{1}; column < row.cols; ++column) {
                if (row.at<float>(0, column) > row.at<float>(0, best)) {
                    best = column;
                }
            }
            return best;
        };

        const int low{peakBand(200.0)};
        const int high{peakBand(3000.0)};
        QVERIFY2(low < high, qPrintable(QStringLiteral("%1 then %2").arg(low).arg(high)));
        QVERIFY(low < 20);
        QVERIFY(high > 40);
    }

    /// The log is only scale invariant above its floor, but the mean removal
    /// afterwards cancels a constant gain outright. That is what lets the
    /// caller pass ordinary [-1, 1] audio to a model trained on integers.
    void aConstantGainChangesNothing()
    {
        const FilterBank bank{rate, 80};
        const cv::Mat quiet{bank.compute(tone(500.0, 800, 0.05F))};
        const cv::Mat loud{bank.compute(tone(500.0, 800, 0.40F))};

        QCOMPARE(quiet.size(), loud.size());
        QVERIFY(cv::norm(quiet - loud, cv::NORM_INF) < 1e-2);
    }
};

QTEST_MAIN(TestFilterBank)
#include "tst_filterbank.moc"
