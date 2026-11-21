// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "attention.h"

#include "perceptiontypes.h"

#include <algorithm>
#include <cmath>

namespace Kerfur {

namespace {

/// Below the top of a person box, as a share of its height, where a head
/// with no confident keypoint is assumed to be.
constexpr double headBelowBoxTop{0.1};

/// Whether `previous` falls on `box`, widened by half its size on each side:
/// somebody moving between two passes leaves the old box, rarely by more.
bool holds(const QRectF &box, const std::optional<QPointF> &previous)
{
    if (!previous || box.isEmpty()) {
        return false;
    }
    const double marginX{box.width() / 2.0};
    const double marginY{box.height() / 2.0};
    return box.adjusted(-marginX, -marginY, marginX, marginY).contains(*previous);
}

double attentionSize(const QRectF &box, const std::optional<QPointF> &previous)
{
    const double area{box.width() * box.height()};
    return holds(box, previous) ? area * attentionStickiness : area;
}

} // namespace

void orderForAttention(QList<AnalysedFace> &faces, const std::optional<QPointF> &previous)
{
    std::stable_sort(faces.begin(),
                     faces.end(),
                     [&previous](const AnalysedFace &left, const AnalysedFace &right) {
                         if (left.priority != right.priority) {
                             return left.priority < right.priority;
                         }
                         return attentionSize(left.box, previous) >
                                attentionSize(right.box, previous);
                     });
}

qsizetype chooseBody(const QList<AnalysedPerson> &people, const std::optional<QPointF> &previous)
{
    qsizetype best{-1};
    for (qsizetype index{0}; index < people.size(); ++index) {
        if (best < 0) {
            best = index;
            continue;
        }
        const AnalysedPerson &candidate{people.at(index)};
        const AnalysedPerson &leader{people.at(best)};
        if (candidate.priority != leader.priority) {
            if (candidate.priority < leader.priority) {
                best = index;
            }
            continue;
        }
        if (attentionSize(candidate.box, previous) > attentionSize(leader.box, previous)) {
            best = index;
        }
    }
    return best;
}

QPointF headPoint(const AnalysedPerson &person, double minimumConfidence)
{
    QPointF sum;
    int counted{0};
    const int lastHeadKeypoint{static_cast<int>(Keypoint::RightEar)};
    for (int index{static_cast<int>(Keypoint::Nose)}; index <= lastHeadKeypoint; ++index) {
        if (index >= person.keypoints.size() || index >= person.keypointScores.size()) {
            break;
        }
        if (person.keypointScores.at(index) < minimumConfidence) {
            continue;
        }
        sum += person.keypoints.at(index);
        ++counted;
    }
    if (counted > 0) {
        return sum / counted;
    }
    return QPointF{person.box.center().x(),
                   person.box.top() + person.box.height() * headBelowBoxTop};
}

double smoothingWeight(double smoothing, double step, double snapScale)
{
    const double bounded{std::clamp(smoothing, 0.0, 1.0)};
    if (bounded <= 0.0 || snapScale <= 0.0) {
        return 0.0;
    }
    return bounded * std::exp(-std::abs(step) / snapScale);
}

} // namespace Kerfur
