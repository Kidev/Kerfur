// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_WORLDMODEL_H
#define KERFUR_WORLDMODEL_H

#include "peoplemodel.h"
#include "perceptiontypes.h"

#include <QObject>
#include <QPointF>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace Kerfur {

/// Everything Kerfur currently believes about the world, in one place.
///
/// Producers push into it and consumers read from it, and nothing else is
/// allowed. The build this replaces let the display layer import the camera and
/// microphone modules and instantiate their objects as QML children, which is
/// why perception state lived in whichever object happened to produce it and why
/// a build without inference hardware was impossible.
///
/// Everything derived from a camera frame notifies through the single
/// `perceptionChanged` signal, so a consumer never observes half of a frame.
class WorldModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool personPresent READ isPersonPresent NOTIFY perceptionChanged)
    Q_PROPERTY(int personCount READ personCount NOTIFY perceptionChanged)
    Q_PROPERTY(bool hasNearestPerson READ hasNearestPerson NOTIFY perceptionChanged)
    Q_PROPERTY(Kerfur::TrackedPerson nearestPerson READ nearestPerson NOTIFY perceptionChanged)
    Q_PROPERTY(QPointF nearestHeadCenter READ nearestHeadCenter NOTIFY perceptionChanged)
    Q_PROPERTY(Kerfur::PeopleModel *people READ people CONSTANT)
    Q_PROPERTY(QString heardText READ heardText NOTIFY heardTextChanged)
    Q_PROPERTY(bool wakeWordActive READ isWakeWordActive NOTIFY wakeWordActiveChanged)
    Q_PROPERTY(bool cameraOnline READ isCameraOnline NOTIFY cameraOnlineChanged)
    Q_PROPERTY(bool microphoneOnline READ isMicrophoneOnline NOTIFY microphoneOnlineChanged)
    Q_PROPERTY(int cameraOrientation READ cameraOrientation NOTIFY cameraOrientationChanged)

public:
    /// No default argument. The QML engine builds a default constructible
    /// singleton itself and never calls `create`, so QML would hold a second
    /// instance that C++ cannot see.
    explicit WorldModel(QObject *parent);

    /// The one instance, reachable from C++ producers as well as from QML.
    /// The QML singleton machinery alone would give the engine its own instance
    /// that no producer could find.
    static WorldModel &instance();
    static WorldModel *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    bool isPersonPresent() const;
    int personCount() const;
    bool hasNearestPerson() const;
    TrackedPerson nearestPerson() const;
    QPointF nearestHeadCenter() const;
    PeopleModel *people();
    QString heardText() const;
    bool isWakeWordActive() const;
    bool isCameraOnline() const;
    bool isMicrophoneOnline() const;
    int cameraOrientation() const;

public slots:
    /// Producers call this, always on this object's own thread. PerceptionPipe
    /// is what gets them there from a worker thread.
    void applyPerceptionFrame(const Kerfur::PerceptionFrame &frame);

    void setHeardText(const QString &text);
    void setWakeWordActive(bool active);
    void setCameraOnline(bool online);
    void setMicrophoneOnline(bool online);
    void setCameraOrientation(int degrees);

signals:
    void perceptionChanged();
    void heardTextChanged();
    void wakeWordActiveChanged();
    void cameraOnlineChanged();
    void microphoneOnlineChanged();
    void cameraOrientationChanged();

private:
    PeopleModel m_people;
    TrackedPerson m_nearestPerson;
    QString m_heardText;
    int m_personCount{0};
    int m_cameraOrientation{0};
    bool m_hasNearestPerson{false};
    bool m_wakeWordActive{false};
    bool m_cameraOnline{false};
    bool m_microphoneOnline{false};
};

} // namespace Kerfur

#endif // KERFUR_WORLDMODEL_H
