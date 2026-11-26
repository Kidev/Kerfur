// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "texturewatch.h"

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Kerfur;

namespace {

// A rectangle captured by a ShaderEffectSource, and one beside it that is not.
const QByteArray scene{R"(
import QtQuick

Item {
    width: 64
    height: 64

    Rectangle {
        id: captured
        objectName: "captured"
        color: "red"
        height: 16
        width: 16
    }

    Rectangle {
        objectName: "outside"
        color: "red"
        height: 16
        width: 16
        x: 32
    }

    ShaderEffectSource {
        objectName: "capture"
        height: 16
        hideSource: true
        sourceItem: captured
        width: 16
        y: 32
    }
}
)"};

} // namespace

/// When the LED screen hears that the face picture was redrawn: after a change
/// inside the captured subtree, and never after one outside it.
class TestTextureWatch : public QObject
{
    Q_OBJECT

public:
    /// The software scene graph on the offscreen platform renders without a
    /// display or a GPU, layers included, on the GUI thread.
    static void initMain()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QSG_RENDER_LOOP", "basic");
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

private slots:
    void init();
    void cleanup();

    void reportsAChangeInsideTheSource();
    void ignoresAChangeOutsideTheSource();
    void reportsNothingWithoutASource();

private:
    /// Renders one frame and delivers what it queued.
    void renderFrame();
    QQuickItem *child(const char *name) const;

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<QQuickItem> m_root;
    TextureWatch *m_watch{nullptr};
};

void TestTextureWatch::init()
{
    m_engine = std::make_unique<QQmlEngine>();
    m_window = std::make_unique<QQuickWindow>();
    m_window->resize(64, 64);

    QQmlComponent component{m_engine.get()};
    component.setData(scene, QUrl{});
    m_root.reset(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(m_root != nullptr, qPrintable(component.errorString()));
    m_root->setParentItem(m_window->contentItem());

    m_watch = new TextureWatch{m_window->contentItem()};
    m_watch->setSource(child("capture"));

    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    // The first frames draw the capture for the first time.
    renderFrame();
    renderFrame();
}

void TestTextureWatch::cleanup()
{
    delete m_watch;
    m_watch = nullptr;
    m_root.reset();
    m_window.reset();
    m_engine.reset();
}

void TestTextureWatch::renderFrame()
{
    QSignalSpy rendered{m_window.get(), &QQuickWindow::afterRendering};
    m_window->update();
    QVERIFY(rendered.wait());
    QCoreApplication::processEvents();
}

QQuickItem *TestTextureWatch::child(const char *name) const
{
    return m_root->findChild<QQuickItem *>(QString::fromLatin1(name));
}

void TestTextureWatch::reportsAChangeInsideTheSource()
{
    QSignalSpy changes{m_watch, &TextureWatch::textureChanged};

    child("captured")->setProperty("color", QColor{Qt::blue});
    renderFrame();

    QTRY_VERIFY(changes.count() > 0);
}

void TestTextureWatch::ignoresAChangeOutsideTheSource()
{
    QSignalSpy changes{m_watch, &TextureWatch::textureChanged};

    child("outside")->setProperty("color", QColor{Qt::blue});
    renderFrame();
    renderFrame();

    QCOMPARE(changes.count(), 0);
}

void TestTextureWatch::reportsNothingWithoutASource()
{
    m_watch->setSource(nullptr);
    renderFrame();
    QSignalSpy changes{m_watch, &TextureWatch::textureChanged};

    child("captured")->setProperty("color", QColor{Qt::blue});
    renderFrame();
    renderFrame();

    QCOMPARE(changes.count(), 0);
}

QTEST_MAIN(TestTextureWatch)

#include "tst_texturewatch.moc"
