// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "skillsource.h"

#include "identitylibrary.h"

#include <QRegularExpression>

namespace Kerfur {

QString declaredSkillName(const QString &source)
{
    // `name` as a whole property name: not `nickname:`, not `day.name:`.
    static const QRegularExpression assignment{
        QStringLiteral("(?:^|[^\\w.])name\\s*:\\s*(?:\"([^\"\\n]*)\"|'([^'\\n]*)')"),
        QRegularExpression::MultilineOption};
    const QRegularExpressionMatch match{assignment.match(source)};
    if (!match.hasMatch()) {
        return {};
    }
    const QString quoted{match.hasCaptured(1) ? match.captured(1) : match.captured(2)};
    return quoted.trimmed();
}

QString skillFileName(const QString &name)
{
    // The same folding a person's id gets, for the same reason: the result
    // names a file, so it must not be able to name any other.
    const QString stem{IdentityLibrary::idFor(name)};
    return stem.isEmpty() ? QString{} : stem + QStringLiteral(".qml");
}

} // namespace Kerfur
