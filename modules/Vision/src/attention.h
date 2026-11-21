// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_ATTENTION_H
#define KERFUR_ATTENTION_H

#include "faceanalysis.h"

#include <QList>
#include <QPointF>

#include <optional>

namespace Kerfur {

/// How much larger another face of the same rank has to be before the eyes
/// leave the one they are already following for it.
inline constexpr double attentionStickiness{1.5};

/// Sorts `faces` into the order the eyes consider them: rank first, then size,
/// with the face that holds `previous` (the point followed on the last pass)
/// counted as `attentionStickiness` times its size. A lone face comes first
/// whatever its rank or name, so a stranger is followed from the first pass,
/// before the gallery has had a say.
void orderForAttention(QList<AnalysedFace> &faces, const std::optional<QPointF> &previous);

/// The body to follow when no face was found, by the same rule as
/// `orderForAttention`, or -1 when `people` is empty.
qsizetype chooseBody(const QList<AnalysedPerson> &people, const std::optional<QPointF> &previous);

/// Where the head of `person` is, normalized: the mean of the nose, eyes and
/// ears the pose stage scored at least `minimumConfidence`, or near the top of
/// the person box when it scored none of them.
QPointF headPoint(const AnalysedPerson &person, double minimumConfidence);

/// The share of the previous position kept when a new one arrives `step` away
/// from it. `smoothing` (0 to 1) applies in full to a step of zero and fades
/// out as the step grows past `snapScale`, so measurement jitter is filtered
/// and a real move goes through at once.
double smoothingWeight(double smoothing, double step, double snapScale);

} // namespace Kerfur

#endif // KERFUR_ATTENTION_H
