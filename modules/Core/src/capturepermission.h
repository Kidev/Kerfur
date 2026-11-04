// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_CAPTUREPERMISSION_H
#define KERFUR_CAPTUREPERMISSION_H

#include <QtCore/qnamespace.h>

#include <functional>

class QObject;

namespace Kerfur {

/// The platform's permission to open a camera or a microphone, asked for once.
///
/// macOS and the browser both refuse capture until the user has answered, and
/// the browser answers asynchronously. The owner calls `ensure` before every
/// start; while a request is in flight it gets `Pending` back, and its retry
/// runs once the platform has answered, whatever the answer was.
///
/// Only a refusal stops capture. A request the platform could not settle (the
/// browser reports that when no device is present) lets the start go ahead,
/// and the capture backend asks again on its own.
class CapturePermission
{
public:
    enum class Device { Camera, Microphone };

    enum class Access {
        /// Capture may start now.
        Allowed,
        /// A request is in flight; the retry passed to `ensure` runs once it
        /// returns.
        Pending,
        /// The user refused, and capture must not start.
        Denied,
    };

    /// What `ensure` does for a given platform answer. Kept apart from the
    /// platform call so that every branch can be exercised without one.
    enum class Step { Proceed, Request, Wait, Refuse };
    static Step nextStep(Qt::PermissionStatus status, bool asked, bool pending);

    explicit CapturePermission(Device device);

    /// Checks the permission and asks for it when nobody has yet. `retry` runs
    /// on `context`'s thread after the answer, and never if `context` is gone.
    /// Must be called from the main thread.
    Access ensure(QObject *context, std::function<void()> retry);

private:
    Device m_device;
    bool m_asked{false};
    bool m_pending{false};
};

} // namespace Kerfur

#endif // KERFUR_CAPTUREPERMISSION_H
