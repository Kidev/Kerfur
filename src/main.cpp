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

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char *argv[])
{
    QGuiApplication app {argc, argv};
    app.setApplicationName("Kerfur");
    app.setApplicationVersion(QStringLiteral(VERSION_TAG));
    app.setOrganizationName("Kidev");

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
