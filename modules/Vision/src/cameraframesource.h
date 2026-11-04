// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_CAMERAFRAMESOURCE_H
#define KERFUR_CAMERAFRAMESOURCE_H

#include "boundedqueue.h"
#include "capturepermission.h"
#include "visiontypes.h"

#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QObject>
#include <QVideoSink>

#include <memory>

class QCamera;
class QVideoFrame;

namespace Kerfur {

/// Capture for every platform that is not the Pi.
///
/// libcamera is not available on a desktop and does not exist in a browser, so
/// this is Qt Multimedia instead. On WebAssembly that is getUserMedia
/// underneath, which imposes three things this class has to respect: the page
/// must be a secure context, the device list only exists after
/// `videoInputsChanged` has fired, and QMediaDevices must be used from the main
/// thread. All three are why opening the camera is retried on a signal rather
/// than done once at start.
///
/// Frames are deep copied into a bounded queue and the worker is nudged. The
/// copy is not avoidable: a QVideoFrame's mapped memory belongs to the capture
/// backend and is gone when the handler returns.
class CameraFrameSource : public QObject
{
    Q_OBJECT

public:
    explicit CameraFrameSource(BoundedQueue<VisionFrame> &queue, QObject *parent = nullptr);
    ~CameraFrameSource() override;

    bool isActive() const;

    /// Captured frames to drop between queued ones. Throttling here rather than
    /// in the worker means the frames nobody will look at are never copied.
    void setFrameStride(int stride);

public slots:
    void start();
    void stop();

signals:
    /// One or more frames are waiting. Connect queued: the worker lives on
    /// another thread and this is emitted from the GUI thread.
    void frameQueued();
    void activeChanged(bool active);

private:
    void openCamera();
    void handleVideoFrame(const QVideoFrame &frame);

    BoundedQueue<VisionFrame> &m_queue;
    QMediaCaptureSession m_session;
    QVideoSink m_sink;
    QMediaDevices m_devices;
    std::unique_ptr<QCamera> m_camera;
    CapturePermission m_permission{CapturePermission::Device::Camera};
    quint64 m_sequence{0};
    quint64 m_received{0};
    int m_frameStride{1};
    bool m_wantActive{false};
};

} // namespace Kerfur

#endif // KERFUR_CAMERAFRAMESOURCE_H
