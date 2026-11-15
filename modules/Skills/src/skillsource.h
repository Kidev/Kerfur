// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_SKILLSOURCE_H
#define KERFUR_SKILLSOURCE_H

#include <QString>

namespace Kerfur {

/// The value of the first `name: "..."` assignment in a skill file, which is
/// the Skill's own name, or empty when there is none. Read from the text so
/// that a skill typed into the panel can be named, and its file chosen, before
/// it is ever instantiated.
QString declaredSkillName(const QString &source);

/// The file a skill called `name` is saved as: the name folded to lower case
/// ASCII letters, digits and dashes, then `.qml`. Empty when nothing usable is
/// left. Nothing in the result can leave the directory it is written to.
QString skillFileName(const QString &name);

} // namespace Kerfur

#endif // KERFUR_SKILLSOURCE_H
