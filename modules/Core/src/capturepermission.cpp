// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "capturepermission.h"

#include <QCoreApplication>
#include <QLoggingCategory>

#if QT_CONFIG(permissions)
#include <QPermissions>
#endif

#include <utility>

namespace {

Q_LOGGING_CATEGORY(permissionLog, "kerfur.core.permission")

} // namespace

namespace Kerfur {

CapturePermission::Step CapturePermission::nextStep(Qt::PermissionStatus status,
                                                    bool asked,
                                                    bool pending)
{
    switch (status) {
    case Qt::PermissionStatus::Granted:
        return Step::Proceed;
    case Qt::PermissionStatus::Denied:
        return Step::Refuse;
    case Qt::PermissionStatus::Undetermined:
        break;
    }
    if (pending) {
        return Step::Wait;
    }
    return asked ? Step::Proceed : Step::Request;
}

CapturePermission::CapturePermission(Device device)
    : m_device{device}
{
}

CapturePermission::Access CapturePermission::ensure(QObject *context, std::function<void()> retry)
{
#if QT_CONFIG(permissions)
    QCoreApplication *application{QCoreApplication::instance()};
    if (application == nullptr) {
        return Access::Allowed;
    }

    const QPermission permission{m_device == Device::Camera ? QPermission{QCameraPermission{}}
                                                            : QPermission{QMicrophonePermission{}}};
    const char *name{m_device == Device::Camera ? "camera" : "microphone"};

    switch (nextStep(application->checkPermission(permission), m_asked, m_pending)) {
    case Step::Proceed:
        return Access::Allowed;
    case Step::Refuse:
        qCWarning(permissionLog) << name << "access denied";
        return Access::Denied;
    case Step::Wait:
        return Access::Pending;
    case Step::Request:
        break;
    }

    qCInfo(permissionLog) << "requesting" << name << "access";
    m_pending = true;
    application->requestPermission(permission,
                                   context,
                                   [this, name, retry = std::move(retry)](const QPermission &result) {
                                       m_pending = false;
                                       m_asked = true;
                                       qCInfo(permissionLog) << name << "access answered"
                                                             << result.status();
                                       if (retry) {
                                           retry();
                                       }
                                   });
    return Access::Pending;
#else
    Q_UNUSED(context);
    Q_UNUSED(retry);
    return Access::Allowed;
#endif
}

} // namespace Kerfur
