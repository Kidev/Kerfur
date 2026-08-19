// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_PEOPLEMODEL_H
#define KERFUR_PEOPLEMODEL_H

#include "perceptiontypes.h"

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

namespace Kerfur {

/// The people the tracker is following, as a list model.
///
/// Rows are kept sorted by track id and updated by diffing rather than by
/// resetting. A reset per frame tears down and rebuilds every delegate at the
/// frame rate, which is both visible as stutter and a steady allocation churn in
/// a process that has to run for weeks.
class PeopleModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("PeopleModel is owned by WorldModel")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        TrackIdRole = Qt::UserRole + 1,
        BoundingBoxRole,
        HeadCenterRole,
        ProximityRole,
        IdentityRole,
        IdentityConfidenceRole,
        EmotionRole,
        EmotionConfidenceRole,
        HeldObjectsRole
    };
    Q_ENUM(Role)

    explicit PeopleModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    /// Replaces the contents with `people`, emitting the narrowest set of model
    /// signals that describes the change.
    void setPeople(const QList<TrackedPerson> &people);

signals:
    void countChanged();

private:
    QList<TrackedPerson> m_people;
};

} // namespace Kerfur

#endif // KERFUR_PEOPLEMODEL_H
