// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "worldmodel.h"

#include <QJSEngine>
#include <QQmlEngine>

namespace Kerfur {

WorldModel::WorldModel(QObject *parent) : QObject{parent}, m_people{this} {}

WorldModel &WorldModel::instance()
{
    static WorldModel model;
    return model;
}

WorldModel *WorldModel::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

    // C++ owned, because the instance outlives any engine: producers on worker
    // threads hold references to it and a garbage collected singleton would be
    // a use after free waiting for a reload.
    QJSEngine::setObjectOwnership(&instance(), QJSEngine::CppOwnership);
    return &instance();
}

bool WorldModel::isPersonPresent() const
{
    return m_personCount > 0;
}

int WorldModel::personCount() const
{
    return m_personCount;
}

bool WorldModel::hasNearestPerson() const
{
    return m_hasNearestPerson;
}

TrackedPerson WorldModel::nearestPerson() const
{
    return m_nearestPerson;
}

QPointF WorldModel::nearestHeadCenter() const
{
    if (!m_hasNearestPerson) {
        return {};
    }
    return m_nearestPerson.headCenter();
}

PeopleModel *WorldModel::people()
{
    return &m_people;
}

QString WorldModel::heardText() const
{
    return m_heardText;
}

bool WorldModel::isWakeWordActive() const
{
    return m_wakeWordActive;
}

bool WorldModel::isCameraOnline() const
{
    return m_cameraOnline;
}

bool WorldModel::isMicrophoneOnline() const
{
    return m_microphoneOnline;
}

int WorldModel::cameraOrientation() const
{
    return m_cameraOrientation;
}

void WorldModel::applyPerceptionFrame(const Kerfur::PerceptionFrame &frame)
{
    const QList<TrackedPerson> people{frame.people()};
    m_people.setPeople(people);
    m_personCount = static_cast<int>(people.size());
    setCameraOrientation(frame.orientationDegrees());

    // Nearest is the largest share of the frame height. It needs no camera
    // calibration and it is what decides who Kerfur looks at, so a tie is
    // resolved by keeping the first element in whatever order the producer
    // supplied the people in.
    m_hasNearestPerson = false;
    qreal bestProximity{-1.0};
    for (const TrackedPerson &person : people) {
        if (person.proximity() > bestProximity) {
            bestProximity = person.proximity();
            m_nearestPerson = person;
            m_hasNearestPerson = true;
        }
    }
    if (!m_hasNearestPerson) {
        m_nearestPerson = TrackedPerson{};
    }

    emit perceptionChanged();
}

void WorldModel::setHeardText(const QString &text)
{
    if (m_heardText == text) {
        return;
    }
    m_heardText = text;
    emit heardTextChanged();
}

void WorldModel::setWakeWordActive(bool active)
{
    if (m_wakeWordActive == active) {
        return;
    }
    m_wakeWordActive = active;
    emit wakeWordActiveChanged();
}

void WorldModel::setCameraOnline(bool online)
{
    if (m_cameraOnline == online) {
        return;
    }
    m_cameraOnline = online;
    emit cameraOnlineChanged();
}

void WorldModel::setMicrophoneOnline(bool online)
{
    if (m_microphoneOnline == online) {
        return;
    }
    m_microphoneOnline = online;
    emit microphoneOnlineChanged();
}

void WorldModel::setCameraOrientation(int degrees)
{
    if (m_cameraOrientation == degrees) {
        return;
    }
    m_cameraOrientation = degrees;
    emit cameraOrientationChanged();
}

} // namespace Kerfur
