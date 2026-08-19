// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "peoplemodel.h"

#include <algorithm>

namespace Kerfur {

PeopleModel::PeopleModel(QObject *parent) : QAbstractListModel{parent} {}

int PeopleModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_people.size());
}

int PeopleModel::count() const
{
    return static_cast<int>(m_people.size());
}

QVariant PeopleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_people.size()) {
        return {};
    }

    const TrackedPerson &person{m_people.at(index.row())};
    switch (role) {
    case TrackIdRole:
        return person.trackId();
    case BoundingBoxRole:
        return person.boundingBox();
    case HeadCenterRole:
        return person.headCenter();
    case ProximityRole:
        return person.proximity();
    case IdentityRole:
        return person.identity();
    case IdentityConfidenceRole:
        return person.identityConfidence();
    case EmotionRole:
        return person.emotion();
    case EmotionConfidenceRole:
        return person.emotionConfidence();
    case HeldObjectsRole:
        return person.heldObjects();
    default:
        return {};
    }
}

QHash<int, QByteArray> PeopleModel::roleNames() const
{
    return {{TrackIdRole, QByteArrayLiteral("trackId")},
            {BoundingBoxRole, QByteArrayLiteral("boundingBox")},
            {HeadCenterRole, QByteArrayLiteral("headCenter")},
            {ProximityRole, QByteArrayLiteral("proximity")},
            {IdentityRole, QByteArrayLiteral("identity")},
            {IdentityConfidenceRole, QByteArrayLiteral("identityConfidence")},
            {EmotionRole, QByteArrayLiteral("emotion")},
            {EmotionConfidenceRole, QByteArrayLiteral("emotionConfidence")},
            {HeldObjectsRole, QByteArrayLiteral("heldObjects")}};
}

void PeopleModel::setPeople(const QList<TrackedPerson> &people)
{
    QList<TrackedPerson> incoming{people};
    std::sort(incoming.begin(),
              incoming.end(),
              [](const TrackedPerson &left, const TrackedPerson &right) {
                  return left.trackId() < right.trackId();
              });

    const qsizetype previousCount{m_people.size()};

    // Both lists are sorted by track id, so one walk finds every insertion,
    // removal and in place update without a second pass or a lookup table.
    qsizetype row{0};
    qsizetype next{0};
    while (row < m_people.size() || next < incoming.size()) {
        if (next >= incoming.size()) {
            beginRemoveRows(QModelIndex{},
                            static_cast<int>(row),
                            static_cast<int>(m_people.size() - 1));
            m_people.remove(row, m_people.size() - row);
            endRemoveRows();
            break;
        }

        if (row >= m_people.size()) {
            beginInsertRows(QModelIndex{}, static_cast<int>(row), static_cast<int>(row));
            m_people.insert(row, incoming.at(next));
            endInsertRows();
            ++row;
            ++next;
            continue;
        }

        const int existingId{m_people.at(row).trackId()};
        const int incomingId{incoming.at(next).trackId()};

        if (existingId == incomingId) {
            m_people[row] = incoming.at(next);
            const QModelIndex changed{index(static_cast<int>(row))};
            emit dataChanged(changed, changed);
            ++row;
            ++next;
        } else if (existingId < incomingId) {
            beginRemoveRows(QModelIndex{}, static_cast<int>(row), static_cast<int>(row));
            m_people.remove(row);
            endRemoveRows();
        } else {
            beginInsertRows(QModelIndex{}, static_cast<int>(row), static_cast<int>(row));
            m_people.insert(row, incoming.at(next));
            endInsertRows();
            ++row;
            ++next;
        }
    }

    if (m_people.size() != previousCount) {
        emit countChanged();
    }
}

} // namespace Kerfur
