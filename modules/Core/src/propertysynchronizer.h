// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_PROPERTYSYNCHRONIZER_H
#define KERFUR_PROPERTYSYNCHRONIZER_H

#include <QMetaObject>
#include <QMetaProperty>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlparserstatus.h>
#include <QtQml/qqmlpropertyvaluesource.h>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <cstddef>

class QQmlProperty;

namespace Kerfur {

/// Keeps up to three properties equal, whichever of them changes.
///
/// The contract is that of `Synchronizer` from Qt.labs.synchronizer, which
/// Qt only ships from 6.10; the Bookworm build runs on 6.7. A property takes
/// part when it is named in one of three ways: the `on` syntax, the
/// `sourceObject`/`sourceProperty` pair, or the `targetObject`/`targetProperty`
/// pair. Aliases declared inside the element are left alone.
///
/// When the component completes, the source pair (or, without one, the `on`
/// property) is copied into the others. From then on a change to any of them
/// is written to the rest. Writes go through the meta-object, so a binding on
/// a written property stays in place and an interceptor such as a `Behavior`
/// still sees the value.
///
/// A written property that answers with another value has bounced: the
/// answer is reported through `valueBounced` and is not propagated. One that
/// does not answer at all is reported through `valueIgnored`. A property
/// without a change signal cannot be observed and is refused with a warning.
class PropertySynchronizer : public QObject, public QQmlPropertyValueSource, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlPropertyValueSource QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(QObject *sourceObject READ sourceObject WRITE setSourceObject NOTIFY
                   sourceObjectChanged FINAL)
    Q_PROPERTY(QString sourceProperty READ sourceProperty WRITE setSourceProperty NOTIFY
                   sourcePropertyChanged FINAL)
    Q_PROPERTY(QObject *targetObject READ targetObject WRITE setTargetObject NOTIFY
                   targetObjectChanged FINAL)
    Q_PROPERTY(QString targetProperty READ targetProperty WRITE setTargetProperty NOTIFY
                   targetPropertyChanged FINAL)

public:
    explicit PropertySynchronizer(QObject *parent = nullptr);

    QObject *sourceObject() const;
    void setSourceObject(QObject *object);

    QString sourceProperty() const;
    void setSourceProperty(const QString &property);

    QObject *targetObject() const;
    void setTargetObject(QObject *object);

    QString targetProperty() const;
    void setTargetProperty(const QString &property);

    /// The property named with the `on` syntax. Set once, before completion.
    void setTarget(const QQmlProperty &property) override;

    void classBegin() override;
    void componentComplete() override;

signals:
    void sourceObjectChanged();
    void sourcePropertyChanged();
    void targetObjectChanged();
    void targetPropertyChanged();

    void valueBounced(QObject *object, const QString &property);
    void valueIgnored(QObject *object, const QString &property);

private slots:
    void synchronizeFromOnProperty();
    void synchronizeFromSource();
    void synchronizeFromTarget();

private:
    static constexpr std::size_t onIndex{0};
    static constexpr std::size_t sourceIndex{1};
    static constexpr std::size_t targetIndex{2};
    static constexpr std::size_t endpointCount{3};

    enum class Result { Untouched, Origin, Accepted, Bounced, Ignored };

    struct Endpoint
    {
        QPointer<QObject> object;
        QMetaProperty property;
        QMetaObject::Connection connection;
    };

    /// One propagation, from the property that changed to every other one.
    struct Round
    {
        std::array<Result, endpointCount> results{};
        std::array<QVariant, endpointCount> written{};
    };

    bool attach(std::size_t index, QObject *object, const QMetaProperty &property);
    bool attachByName(std::size_t index, QObject *object, const QString &name);
    void detach(std::size_t index);
    void synchronize(std::size_t index);

    std::array<Endpoint, endpointCount> m_endpoints{};
    QPointer<QObject> m_onObject;
    QMetaProperty m_onProperty;
    QPointer<QObject> m_sourceObject;
    QString m_sourceProperty;
    QPointer<QObject> m_targetObject;
    QString m_targetProperty;
    Round *m_round{nullptr};
    bool m_complete{false};
};

} // namespace Kerfur

#endif // KERFUR_PROPERTYSYNCHRONIZER_H
