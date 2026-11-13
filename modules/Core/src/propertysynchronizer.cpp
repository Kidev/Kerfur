// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "propertysynchronizer.h"

#include <QByteArray>
#include <QMetaMethod>
#include <QMetaType>
#include <QScopedValueRollback>
#include <QtQml/qqmlinfo.h>
#include <QtQml/qqmlproperty.h>

namespace Kerfur {

PropertySynchronizer::PropertySynchronizer(QObject *parent)
    : QObject{parent}
{
}

QObject *PropertySynchronizer::sourceObject() const
{
    return m_sourceObject.data();
}

void PropertySynchronizer::setSourceObject(QObject *object)
{
    if (m_sourceObject == object) {
        return;
    }
    m_sourceObject = object;
    if (m_complete) {
        attachByName(sourceIndex, m_sourceObject, m_sourceProperty);
    }
    emit sourceObjectChanged();
}

QString PropertySynchronizer::sourceProperty() const
{
    return m_sourceProperty;
}

void PropertySynchronizer::setSourceProperty(const QString &property)
{
    if (m_sourceProperty == property) {
        return;
    }
    m_sourceProperty = property;
    if (m_complete) {
        attachByName(sourceIndex, m_sourceObject, m_sourceProperty);
    }
    emit sourcePropertyChanged();
}

QObject *PropertySynchronizer::targetObject() const
{
    return m_targetObject.data();
}

void PropertySynchronizer::setTargetObject(QObject *object)
{
    if (m_targetObject == object) {
        return;
    }
    m_targetObject = object;
    if (m_complete) {
        attachByName(targetIndex, m_targetObject, m_targetProperty);
    }
    emit targetObjectChanged();
}

QString PropertySynchronizer::targetProperty() const
{
    return m_targetProperty;
}

void PropertySynchronizer::setTargetProperty(const QString &property)
{
    if (m_targetProperty == property) {
        return;
    }
    m_targetProperty = property;
    if (m_complete) {
        attachByName(targetIndex, m_targetObject, m_targetProperty);
    }
    emit targetPropertyChanged();
}

void PropertySynchronizer::setTarget(const QQmlProperty &property)
{
    // QMetaProperty cannot address one field of a value type such as
    // `font.pixelSize`; writing the whole value instead would be wrong.
    if (property.name().contains(QLatin1Char{'.'})) {
        qmlWarning(this) << "Cannot synchronize the grouped property " << property.name();
        return;
    }
    m_onObject = property.object();
    m_onProperty = property.property();
}

void PropertySynchronizer::classBegin()
{
}

void PropertySynchronizer::componentComplete()
{
    m_complete = true;

    const bool hasOnProperty{attach(onIndex, m_onObject, m_onProperty)};
    const bool hasSource{attachByName(sourceIndex, m_sourceObject, m_sourceProperty)};
    attachByName(targetIndex, m_targetObject, m_targetProperty);

    if (hasSource) {
        synchronize(sourceIndex);
    } else if (hasOnProperty) {
        synchronize(onIndex);
    }
}

void PropertySynchronizer::synchronizeFromOnProperty()
{
    synchronize(onIndex);
}

void PropertySynchronizer::synchronizeFromSource()
{
    synchronize(sourceIndex);
}

void PropertySynchronizer::synchronizeFromTarget()
{
    synchronize(targetIndex);
}

bool PropertySynchronizer::attach(std::size_t index, QObject *object, const QMetaProperty &property)
{
    detach(index);
    if (object == nullptr || !property.isValid()) {
        return false;
    }
    if (!property.hasNotifySignal()) {
        qmlWarning(this) << "Property " << QString::fromLatin1(property.name())
                         << " has no change signal and cannot be synchronized";
        return false;
    }

    static constexpr std::array<const char *, endpointCount> slotSignatures{
        "synchronizeFromOnProperty()",
        "synchronizeFromSource()",
        "synchronizeFromTarget()",
    };
    const int slotIndex{staticMetaObject.indexOfMethod(slotSignatures[index])};
    Q_ASSERT(slotIndex >= 0);

    Endpoint &endpoint{m_endpoints[index]};
    endpoint.connection = connect(object,
                                  property.notifySignal(),
                                  this,
                                  staticMetaObject.method(slotIndex));
    if (!endpoint.connection) {
        qmlWarning(this) << "Cannot observe property " << QString::fromLatin1(property.name());
        return false;
    }
    endpoint.object = object;
    endpoint.property = property;
    return true;
}

bool PropertySynchronizer::attachByName(std::size_t index, QObject *object, const QString &name)
{
    if (object == nullptr || name.isEmpty()) {
        detach(index);
        return false;
    }
    const QMetaObject *metaObject{object->metaObject()};
    const int propertyIndex{metaObject->indexOfProperty(name.toUtf8().constData())};
    if (propertyIndex < 0) {
        detach(index);
        qmlWarning(this) << "Object has no property called " << name;
        return false;
    }
    return attach(index, object, metaObject->property(propertyIndex));
}

void PropertySynchronizer::detach(std::size_t index)
{
    Endpoint &endpoint{m_endpoints[index]};
    disconnect(endpoint.connection);
    endpoint = Endpoint{};
}

void PropertySynchronizer::synchronize(std::size_t index)
{
    const Endpoint &origin{m_endpoints[index]};
    if (origin.object.isNull()) {
        return;
    }
    const QVariant value{origin.property.read(origin.object)};

    // A change signal while a round is running is the answer to one of its
    // writes. Logic on a property may change it several times in a row, so
    // only the last answer counts.
    if (m_round != nullptr) {
        if (m_round->results[index] != Result::Origin) {
            m_round->results[index] = value == m_round->written[index] ? Result::Accepted
                                                                       : Result::Bounced;
        }
        return;
    }

    Round round{};
    round.results[index] = Result::Origin;
    {
        const QScopedValueRollback guard{m_round, &round};
        for (std::size_t other{0}; other < endpointCount; ++other) {
            const Endpoint &endpoint{m_endpoints[other]};
            if (other == index || endpoint.object.isNull()) {
                continue;
            }
            round.results[other] = Result::Ignored;

            QVariant converted{value};
            const QMetaType type{endpoint.property.metaType()};
            if (type != QMetaType::fromType<QVariant>() && converted.metaType() != type
                && !converted.convert(type)) {
                qmlWarning(this) << "Cannot convert from " << value.metaType().name() << " to "
                                 << type.name();
                continue;
            }
            round.written[other] = converted;
            // A write that makes the property answer, here or in a handler it
            // triggers, re-enters synchronize() and records the answer.
            endpoint.property.write(endpoint.object, converted);
        }
    }

    for (std::size_t other{0}; other < endpointCount; ++other) {
        const Endpoint &endpoint{m_endpoints[other]};
        if (endpoint.object.isNull()) {
            continue;
        }
        const QString name{QString::fromLatin1(endpoint.property.name())};
        if (round.results[other] == Result::Bounced) {
            emit valueBounced(endpoint.object, name);
        } else if (round.results[other] == Result::Ignored) {
            emit valueIgnored(endpoint.object, name);
        }
    }
}

} // namespace Kerfur
