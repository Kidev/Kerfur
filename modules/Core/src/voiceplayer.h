// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_VOICEPLAYER_H
#define KERFUR_VOICEPLAYER_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace Kerfur {

/// Plays one enrolled recording at a time, for the People section to let a
/// person hear what was taught.
///
/// The desktop build plays through Qt Multimedia. The web build hands the
/// file's bytes to a browser `Audio` element instead: Qt's own wasm player
/// routes a local file through a Web Audio context created on the spot, which
/// the browser may leave suspended, and the recording then plays in silence.
class VoicePlayer : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The recording being played, or an empty URL.
    Q_PROPERTY(QUrl playing READ playing NOTIFY playingChanged)
    /// Why the last recording could not be played, cleared by the next one
    /// that plays.
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)

public:
    explicit VoicePlayer(QObject *parent = nullptr);
    ~VoicePlayer() override;

    QUrl playing() const { return m_playing; }
    QString problem() const { return m_problem; }

    /// Plays `recording`, stopping whatever was playing. Stops instead when
    /// `recording` is the one already playing.
    Q_INVOKABLE void toggle(const QUrl &recording);
    Q_INVOKABLE void stop();

signals:
    void playingChanged();
    void problemChanged();

private slots:
    /// The recording started under `token` reached its end, or failed.
    void applyPlaybackEnded(int token, bool failed);

private:
    struct Backend;

    void play(const QUrl &recording);
    void setPlaying(const QUrl &recording);
    void setProblem(const QString &problem);

    std::unique_ptr<Backend> m_backend;
    QUrl m_playing;
    QString m_problem;
    /// Counts the recordings started, so an end reported for an earlier one
    /// cannot stop a later one.
    int m_token{0};
};

} // namespace Kerfur

#endif // KERFUR_VOICEPLAYER_H
