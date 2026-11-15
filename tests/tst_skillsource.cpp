// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "skillsource.h"

#include <QTest>

using namespace Kerfur;

class TestSkillSource : public QObject
{
    Q_OBJECT

private slots:
    void readsTheSkillName()
    {
        const QString source{QStringLiteral("import Skills\n\nSkill {\n    id: day\n\n"
                                            "    name: \"calories\"\n"
                                            "    description: \"Counts calories\"\n}\n")};
        QCOMPARE(declaredSkillName(source), QStringLiteral("calories"));
    }

    void readsASingleQuotedNameOnOneLine()
    {
        QCOMPARE(declaredSkillName(QStringLiteral("Skill { id: s; name: ' Greeting ' }")),
                 QStringLiteral("Greeting"));
    }

    void ignoresPropertiesThatOnlyEndInName()
    {
        const QString source{QStringLiteral("Skill {\n    nickname: \"no\"\n"
                                            "    day.name: \"nor this\"\n}\n")};
        QVERIFY(declaredSkillName(source).isEmpty());
    }

    void findsNoNameWhereNoneIsDeclared()
    {
        QVERIFY(declaredSkillName(QStringLiteral("Skill { id: s }")).isEmpty());
    }

    void foldsTheNameIntoAFileName()
    {
        QCOMPARE(skillFileName(QStringLiteral("Good Morning")), QStringLiteral("good-morning.qml"));
    }

    void refusesANameThatCouldLeaveTheDirectory()
    {
        QCOMPARE(skillFileName(QStringLiteral("../../etc/passwd")),
                 QStringLiteral("etcpasswd.qml"));
        QVERIFY(skillFileName(QStringLiteral("/../")).isEmpty());
        QVERIFY(skillFileName(QString{}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSkillSource)

#include "tst_skillsource.moc"
