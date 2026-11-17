// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_SESSION_H
#define KERFUR_SESSION_H

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace Kerfur {

/// How this run of Kerfur was started, and how it ends.
///
/// A light session is started with `--light` (the web page passes it for a
/// `?light` address). It loads no model, runs no skill and opens neither the
/// camera nor the microphone. The face, the screen and the glow run as usual,
/// and the control panel shows only the sections for them.
class Session : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Whether this run started without models, camera and microphone.
    Q_PROPERTY(bool light READ isLight CONSTANT)

public:
    explicit Session(QObject *parent = nullptr);

    /// The command line option that starts a light session.
    static QString lightOption();

    /// Whether `arguments` (a program's command line, program name first)
    /// asks for a light session.
    static bool lightIn(const QStringList &arguments);

    /// Whether this process runs a light session. Read once from the
    /// application's arguments, so it needs the QCoreApplication to exist.
    static bool light();

    bool isLight() const { return light(); }

    /// Quits. In a browser the page itself is the application, so it asks the
    /// browser to close the tab, and goes back one page where the browser
    /// refuses (it lets a script close only a tab with a single page in its
    /// history, or one a script opened).
    Q_INVOKABLE void end();
};

} // namespace Kerfur

#endif // KERFUR_SESSION_H
