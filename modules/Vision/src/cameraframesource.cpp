// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "cameraframesource.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QDateTime>
#include <QImage>
#include <QLoggingCategory>
#include <QVideoFrame>

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Kerfur {

namespace {

Q_LOGGING_CATEGORY(cameraLog, "kerfur.vision.camera")

// Large enough that a face crop still has pixels in it at conversational
// distance, small enough that the per frame copy and the letterbox stay cheap.
// The pose stage reduces everything to 640 square regardless.
constexpr QSize preferredResolution{1280, 720};

/// The format whose resolution is nearest the preferred one, by area. An empty
/// list is normal on WebAssembly, where the browser chooses.
QCameraFormat nearestFormat(const QList<QCameraFormat> &formats)
{
    QCameraFormat best;
    qint64 bestDistance{std::numeric_limits<qint64>::max()};
    const qint64 target{static_cast<qint64>(preferredResolution.width())
                        * preferredResolution.height()};

    for (const QCameraFormat &format : formats) {
        const QSize resolution{format.resolution()};
        if (resolution.isEmpty()) {
            continue;
        }
        const qint64 area{static_cast<qint64>(resolution.width()) * resolution.height()};
        const qint64 distance{std::abs(area - target)};
        if (distance < bestDistance) {
            bestDistance = distance;
            best = format;
        }
    }
    return best;
}

} // namespace

CameraFrameSource::CameraFrameSource(BoundedQueue<VisionFrame> &queue, QObject *parent)
    : QObject{parent}, m_queue{queue}
{
    m_session.setVideoSink(&m_sink);
    connect(&m_sink, &QVideoSink::videoFrameChanged, this, &CameraFrameSource::handleVideoFrame);

    // The device list is asynchronous on WebAssembly and merely changeable
    // everywhere else, so opening is driven by this rather than attempted once.
    connect(&m_devices, &QMediaDevices::videoInputsChanged, this, [this]() {
        if (m_wantActive && !isActive()) {
            openCamera();
        }
    });
}

CameraFrameSource::~CameraFrameSource()
{
    stop();
}

bool CameraFrameSource::isActive() const
{
    return m_camera != nullptr && m_camera->isActive();
}

void CameraFrameSource::setFrameStride(int stride)
{
    m_frameStride = std::max(1, stride);
}

void CameraFrameSource::start()
{
    m_wantActive = true;
    openCamera();
}

void CameraFrameSource::stop()
{
    m_wantActive = false;
    if (m_camera) {
        m_camera->stop();
        m_session.setCamera(nullptr);
        m_camera.reset();
        emit activeChanged(false);
    }
    // Dropping what is already queued. Restarting the camera and then handing
    // the worker frames from before the pause would make the tracker associate
    // across a gap it has no way to reason about.
    m_queue.clear();
}

void CameraFrameSource::openCamera()
{
    if (isActive()) {
        return;
    }

    const auto retry{[this]() {
        if (m_wantActive && !isActive()) {
            openCamera();
        }
    }};
    switch (m_permission.ensure(this, retry)) {
    case CapturePermission::Access::Allowed:
        break;
    case CapturePermission::Access::Pending:
        return;
    case CapturePermission::Access::Denied:
        qCWarning(cameraLog) << "camera access denied; the headless pipeline stays idle";
        return;
    }

    const QCameraDevice device{QMediaDevices::defaultVideoInput()};
    if (device.isNull()) {
        qCInfo(cameraLog) << "no video input yet";
        return;
    }

    m_camera = std::make_unique<QCamera>(device);
    const QCameraFormat format{nearestFormat(device.videoFormats())};
    if (!format.isNull()) {
        m_camera->setCameraFormat(format);
    }

    connect(m_camera.get(), &QCamera::errorOccurred, this, [](QCamera::Error error, const QString &message) {
        qCWarning(cameraLog) << "camera error" << error << message;
    });

    m_session.setCamera(m_camera.get());
    m_camera->start();
    emit activeChanged(m_camera->isActive());
}

void CameraFrameSource::handleVideoFrame(const QVideoFrame &frame)
{
    if (!m_wantActive || !frame.isValid()) {
        return;
    }

    ++m_received;
    if ((m_received % static_cast<quint64>(m_frameStride)) != 0) {
        return;
    }

    QImage image{frame.toImage()};
    if (image.isNull()) {
        return;
    }
    // OpenCV works in BGR and the conversion is in place, so this costs one
    // pass over an image that has already been copied out of the frame.
    image.convertTo(QImage::Format_BGR888);

    const cv::Mat wrapped{image.height(),
                          image.width(),
                          CV_8UC3,
                          image.bits(),
                          static_cast<size_t>(image.bytesPerLine())};

    VisionFrame visionFrame;
    visionFrame.image = wrapped.clone();
    visionFrame.timestampMs = QDateTime::currentMSecsSinceEpoch();
    visionFrame.sequence = ++m_sequence;

    m_queue.push(std::move(visionFrame));
    emit frameQueued();
}

} // namespace Kerfur
