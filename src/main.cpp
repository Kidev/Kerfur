/**
 * Kerfur
 *
 * By Alexandre 'kidev' Poumaroux
 *
 * Based on the cute robot from mrdrnose's Voices of the Void
 * https://mrdrnose.itch.io/votv
 *
 * Copyright (C) 2025 Alexandre 'kidev' Poumaroux
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifdef DEBUG_BUILD
#define DEBUG_LOG "true"
#else
#define DEBUG_LOG "false"
#endif

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QSurfaceFormat>

#ifdef KERFUR_PERCEPTION_CPU
#include "visionproducer.h"
#include "worldmodel.h"
#endif

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral(PROJECT_ORG));
    QCoreApplication::setOrganizationDomain(QStringLiteral(PROJECT_DOMAIN));
    QCoreApplication::setApplicationName(QStringLiteral(PROJECT_TITLE));

    QLoggingCategory::setFilterRules(
        "*.ffmpeg.*=false\n*.multimedia.*=false\n*.warning=true\n*.debug=" %
        QStringLiteral(DEBUG_LOG) % "\nqt.*.debug=false");

    QGuiApplication app{argc, argv};
    app.setApplicationVersion(QStringLiteral(VERSION_TAG));

#ifdef KERFUR_PERCEPTION_CPU
    // Instantiated at the application root and never exposed to QML. The
    // producer pushes into WorldModel and consumers read WorldModel; nothing
    // imports a producer, which is what makes an inference free build a module
    // list change rather than a code change.
    //
    // Constructed before the engine so that the first frame the face renders
    // already reflects whether a camera was found.
    //
    // Its own key rather than `Shader/faceTrackingEnabled`, and off by default,
    // because Face.qml still drives the legacy FaceTracker and the two stacks
    // would open the same camera device. The key goes away with the legacy
    // stack, when Face.qml starts reading WorldModel.
    Kerfur::VisionProducer vision{Kerfur::WorldModel::instance()};
    {
        const QSettings settings;
        vision.setEnabled(
            settings.value(QStringLiteral("Perception/cpuPipelineEnabled"), false).toBool());
    }
#endif

    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("qml", "Main");
    return app.exec();
}
