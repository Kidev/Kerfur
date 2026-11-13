// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "propertysynchronizer.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <memory>

using namespace Kerfur;

/// A property with a change signal that can be told to clamp what it is given,
/// or to swallow it without a word.
class Holder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(int count READ count WRITE setCount NOTIFY countChanged)
    Q_PROPERTY(double limit MEMBER m_limit)
    Q_PROPERTY(bool deaf MEMBER m_deaf)
    Q_PROPERTY(double unobservable MEMBER m_unobservable)

public:
    using QObject::QObject;

    double value() const { return m_value; }
    void setValue(double value)
    {
        if (m_deaf) {
            return;
        }
        const double next{std::min(value, m_limit)};
        if (next == m_value) {
            return;
        }
        m_value = next;
        emit valueChanged();
    }

    int count() const { return m_count; }
    void setCount(int count)
    {
        if (count == m_count) {
            return;
        }
        m_count = count;
        emit countChanged();
    }

signals:
    void valueChanged();
    void countChanged();

private:
    double m_value{0.0};
    int m_count{0};
    double m_limit{std::numeric_limits<double>::infinity()};
    bool m_deaf{false};
    double m_unobservable{0.0};
};

/// The stand-in for Qt.labs.synchronizer that lets the Bookworm build, on
/// Qt 6.7, load the face and the settings sliders.
///
/// The fixtures are QML documents, created the way the application creates
/// the face and the control panel, so the `on` syntax, the order of
/// completion and the bindings involved are the real ones.
class TestPropertySynchronizer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void onSyntaxStartsFromTheSourcePair();
    void onSyntaxStartsFromItselfWithoutASourcePair();
    void sourceAndTargetPairsWithoutOnSyntax();
    void changesFlowBothWays();
    void bindingOnAWrittenPropertySurvives();
    void acceptedWriteReportsNothing();
    void clampedWriteIsReportedAsBouncedAndNotPropagated();
    void swallowedWriteIsReportedAsIgnored();
    void valueIsCoercedToTheTargetType();
    void propertyWithoutChangeSignalIsRefused();
    void retargetingFollowsTheNewProperty();
    void destroyedEndpointIsSkipped();

private:
    std::unique_ptr<QObject> create(const QByteArray &body);

    QQmlEngine m_engine;
};

void TestPropertySynchronizer::initTestCase()
{
    qmlRegisterType<PropertySynchronizer>("KerfurTest", 1, 0, "PropertySynchronizer");
    qmlRegisterType<Holder>("KerfurTest", 1, 0, "Holder");
}

std::unique_ptr<QObject> TestPropertySynchronizer::create(const QByteArray &body)
{
    QQmlComponent component{&m_engine};
    component.setData("import QtQml\nimport KerfurTest\n" + body, QUrl{});
    std::unique_ptr<QObject> object{component.create()};
    if (!object) {
        qWarning().noquote() << component.errorString();
    }
    return object;
}

static PropertySynchronizer *synchronizerOf(QObject *root)
{
    return root->property("synchronizer").value<PropertySynchronizer *>();
}

static Holder *holderOf(QObject *root, const char *name)
{
    return root->property(name).value<Holder *>();
}

void TestPropertySynchronizer::onSyntaxStartsFromTheSourcePair()
{
    const std::unique_ptr<QObject> root{create(R"(
        Holder {
            id: control
            value: 0.75
            property Holder settings: Holder { value: 0.25 }
            property alias synchronizer: sync
            PropertySynchronizer on value {
                id: sync
                sourceObject: control.settings
                sourceProperty: "value"
            }
        })")};
    QVERIFY(root);
    QCOMPARE(root->property("value").toDouble(), 0.25);
    QCOMPARE(holderOf(root.get(), "settings")->value(), 0.25);
}

void TestPropertySynchronizer::onSyntaxStartsFromItselfWithoutASourcePair()
{
    const std::unique_ptr<QObject> root{create(R"(
        Holder {
            id: control
            value: 0.75
            property Holder settings: Holder { value: 0.25 }
            property alias synchronizer: sync
            PropertySynchronizer on value {
                id: sync
                targetObject: control.settings
                targetProperty: "value"
            }
        })")};
    QVERIFY(root);
    QCOMPARE(root->property("value").toDouble(), 0.75);
    QCOMPARE(holderOf(root.get(), "settings")->value(), 0.75);
}

void TestPropertySynchronizer::sourceAndTargetPairsWithoutOnSyntax()
{
    const std::unique_ptr<QObject> root{create(R"(
        QtObject {
            id: face
            property real angle: 90
            property Holder settings: Holder { value: 30 }
            property alias synchronizer: sync
            property PropertySynchronizer held: PropertySynchronizer {
                id: sync
                sourceObject: face
                sourceProperty: "angle"
                targetObject: face.settings
                targetProperty: "value"
            }
        })")};
    QVERIFY(root);
    QCOMPARE(holderOf(root.get(), "settings")->value(), 90.0);

    holderOf(root.get(), "settings")->setValue(45.0);
    QCOMPARE(root->property("angle").toDouble(), 45.0);
}

void TestPropertySynchronizer::changesFlowBothWays()
{
    const std::unique_ptr<QObject> root{create(R"(
        Holder {
            id: control
            property Holder settings: Holder { value: 0.25 }
            property alias synchronizer: sync
            PropertySynchronizer on value {
                id: sync
                sourceObject: control.settings
                sourceProperty: "value"
            }
        })")};
    QVERIFY(root);
    Holder *control{qobject_cast<Holder *>(root.get())};
    Holder *settings{holderOf(root.get(), "settings")};

    control->setValue(0.5);
    QCOMPARE(settings->value(), 0.5);

    settings->setValue(0.125);
    QCOMPARE(control->value(), 0.125);
}

void TestPropertySynchronizer::bindingOnAWrittenPropertySurvives()
{
    const std::unique_ptr<QObject> root{create(R"(
        QtObject {
            id: model
            property real base: 1
            property real bound: base * 2
            property Holder input: Holder { value: 50 }
            property alias synchronizer: sync
            property PropertySynchronizer held: PropertySynchronizer {
                id: sync
                sourceObject: model.input
                sourceProperty: "value"
                targetObject: model
                targetProperty: "bound"
            }
        })")};
    QVERIFY(root);
    QCOMPARE(root->property("bound").toDouble(), 50.0);

    root->setProperty("base", 4.0);
    QCOMPARE(root->property("bound").toDouble(), 8.0);
    QCOMPARE(holderOf(root.get(), "input")->value(), 8.0);
}

void TestPropertySynchronizer::acceptedWriteReportsNothing()
{
    const std::unique_ptr<QObject> root{create(R"(
        Holder {
            id: control
            limit: 10
            property Holder settings: Holder {}
            property alias synchronizer: sync
            PropertySynchronizer on value {
                id: sync
                sourceObject: control.settings
                sourceProperty: "value"
            }
        })")};
    QVERIFY(root);
    QSignalSpy bounced{synchronizerOf(root.get()), &PropertySynchronizer::valueBounced};
    QSignalSpy ignored{synchronizerOf(root.get()), &PropertySynchronizer::valueIgnored};

    holderOf(root.get(), "settings")->setValue(5.0);
    QCOMPARE(root->property("value").toDouble(), 5.0);
    QCOMPARE(bounced.count(), 0);
    QCOMPARE(ignored.count(), 0);
}

void TestPropertySynchronizer::clampedWriteIsReportedAsBouncedAndNotPropagated()
{
    const std::unique_ptr<QObject> root{create(R"(
        Holder {
            id: control
            limit: 10
            property Holder settings: Holder {}
            property alias synchronizer: sync
            PropertySynchronizer on value {
                id: sync
                sourceObject: control.settings
                sourceProperty: "value"
            }
        })")};
    QVERIFY(root);
    QSignalSpy bounced{synchronizerOf(root.get()), &PropertySynchronizer::valueBounced};

    holderOf(root.get(), "settings")->setValue(20.0);
    QCOMPARE(root->property("value").toDouble(), 10.0);
    QCOMPARE(holderOf(root.get(), "settings")->value(), 20.0);
    QCOMPARE(bounced.count(), 1);
    QCOMPARE(bounced.first().at(0).value<QObject *>(), root.get());
    QCOMPARE(bounced.first().at(1).toString(), QStringLiteral("value"));
}

void TestPropertySynchronizer::swallowedWriteIsReportedAsIgnored()
{
    const std::unique_ptr<QObject> root{create(R"(
        Holder {
            id: control
            property Holder settings: Holder {}
            property alias synchronizer: sync
            PropertySynchronizer on value {
                id: sync
                sourceObject: control.settings
                sourceProperty: "value"
            }
        })")};
    QVERIFY(root);
    QSignalSpy ignored{synchronizerOf(root.get()), &PropertySynchronizer::valueIgnored};

    root->setProperty("deaf", true);
    holderOf(root.get(), "settings")->setValue(3.0);
    QCOMPARE(root->property("value").toDouble(), 0.0);
    QCOMPARE(ignored.count(), 1);
    QCOMPARE(ignored.first().at(0).value<QObject *>(), root.get());
}

void TestPropertySynchronizer::valueIsCoercedToTheTargetType()
{
    const std::unique_ptr<QObject> root{create(R"(
        Holder {
            id: control
            property Holder settings: Holder { value: 3 }
            property alias synchronizer: sync
            PropertySynchronizer on count {
                id: sync
                sourceObject: control.settings
                sourceProperty: "value"
            }
        })")};
    QVERIFY(root);
    QCOMPARE(root->property("count").toInt(), 3);

    qobject_cast<Holder *>(root.get())->setCount(7);
    QCOMPARE(holderOf(root.get(), "settings")->value(), 7.0);
}

void TestPropertySynchronizer::propertyWithoutChangeSignalIsRefused()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression{QStringLiteral("no change signal")});
    const std::unique_ptr<QObject> root{create(R"(
        Holder {
            id: control
            value: 4
            property Holder settings: Holder { unobservable: 9 }
            property alias synchronizer: sync
            PropertySynchronizer on value {
                id: sync
                targetObject: control.settings
                targetProperty: "unobservable"
            }
        })")};
    QVERIFY(root);
    QCOMPARE(holderOf(root.get(), "settings")->property("unobservable").toDouble(), 9.0);
    QCOMPARE(root->property("value").toDouble(), 4.0);
}

void TestPropertySynchronizer::retargetingFollowsTheNewProperty()
{
    const std::unique_ptr<QObject> root{create(R"(
        Holder {
            id: control
            property Holder first: Holder { value: 1 }
            property Holder second: Holder { value: 2 }
            property alias synchronizer: sync
            PropertySynchronizer on value {
                id: sync
                sourceObject: control.first
                sourceProperty: "value"
            }
        })")};
    QVERIFY(root);
    QCOMPARE(root->property("value").toDouble(), 1.0);

    synchronizerOf(root.get())->setSourceObject(holderOf(root.get(), "second"));
    holderOf(root.get(), "first")->setValue(5.0);
    QCOMPARE(root->property("value").toDouble(), 1.0);

    holderOf(root.get(), "second")->setValue(6.0);
    QCOMPARE(root->property("value").toDouble(), 6.0);
}

void TestPropertySynchronizer::destroyedEndpointIsSkipped()
{
    Holder source;
    source.setValue(2.0);
    auto target{std::make_unique<Holder>()};

    PropertySynchronizer synchronizer;
    synchronizer.setSourceObject(&source);
    synchronizer.setSourceProperty(QStringLiteral("value"));
    synchronizer.setTargetObject(target.get());
    synchronizer.setTargetProperty(QStringLiteral("value"));
    synchronizer.classBegin();
    synchronizer.componentComplete();
    QCOMPARE(target->value(), 2.0);

    target.reset();
    QSignalSpy ignored{&synchronizer, &PropertySynchronizer::valueIgnored};
    source.setValue(3.0);
    QCOMPARE(ignored.count(), 0);
}

QTEST_MAIN(TestPropertySynchronizer)

#include "tst_propertysynchronizer.moc"
