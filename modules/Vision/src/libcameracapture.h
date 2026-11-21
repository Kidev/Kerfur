// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_LIBCAMERACAPTURE_H
#define KERFUR_LIBCAMERACAPTURE_H

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>

#include <atomic>

namespace Kerfur {

/// One camera that only libcamera can drive, such as a Raspberry Pi CSI module.
///
/// Cameras that V4L2 can open as they are (USB webcams, which libcamera also
/// lists through its uvcvideo handler) are left out: QCamera already offers
/// them, and listing them twice would give the panel two rows for one device.
struct LibcameraDevice
{
    /// libcamera's id, which is what `libcamerasrc camera-name` selects by.
    QString name;
    /// The sensor model, such as `imx708_wide`.
    QString model;
    /// The NV12 output sizes worth offering, largest first: those with the
    /// sensor's own aspect ratio (any other is a crop of the field of view)
    /// and at least `minimumOfferedWidth` wide.
    QList<QSize> sizes;
    /// The sensor's full active area, read from the device's properties
    /// rather than from its output sizes, which can stop short of it.
    QSize fullSize;
};

/// The id FaceTracker stores in `cameraId` for a libcamera device, and the
/// reverse. The prefix keeps the two id spaces apart: a QCamera id is a device
/// node or a browser token, and neither starts with it.
QByteArray libcameraCameraId(const QString &name);
bool isLibcameraCameraId(const QByteArray &id);
QString libcameraNameFromId(const QByteArray &id);

/// The cameras libcamera can see that V4L2 cannot, or an empty list when
/// GStreamer or its libcamera plugin is not installed.
QList<LibcameraDevice> libcameraDevices();

/// Below this width a capture is too small for any stage to use.
inline constexpr int minimumOfferedWidth{640};

/// The sizes of `offered` worth listing for a sensor of `fullSize`, largest
/// first: same aspect ratio, at least `minimumOfferedWidth` wide, and always
/// including the full size and the 2x2 binned size.
QList<QSize> usefulCaptureSizes(const QList<QSize> &offered, const QSize &fullSize);

/// The output size for a request: the exact size when offered, otherwise the
/// smallest offered size that covers it, otherwise the largest offered.
QSize chooseCaptureSize(const QList<QSize> &offered, const QSize &requested);

/// The sensor mode that keeps the whole field of view for `output`: the 2x2
/// binned mode (half the full size on each side) when the output fits inside
/// it, otherwise the full size. Left to itself libcamera picks the mode
/// closest to the output, which for some sizes is a centre crop.
QSize fullViewSensorMode(const QSize &fullSize, const QSize &output);

/// What the capture pipeline is built from.
struct LibcameraPipelineConfig
{
    QString cameraName;
    QSize output;
    /// Empty to let libcamera choose the sensor mode.
    QSize sensorMode;
    int sensorDepth{10};
    /// Ask for BT.601 limited range, which is the matrix the NV12 to RGB
    /// conversion assumes. The fallback pipeline leaves the colour space to
    /// libcamera.
    bool bt601{true};
};

/// The `gst_parse_launch` description of the capture pipeline.
///
/// There is never a frame rate in the caps. libcamera then delivers what the
/// sensor mode allows (thirty a second up to the binned mode of the IMX708,
/// fourteen at its full size), and a rate above what the mode can do stalls
/// the pipeline without an error.
///
/// A stream to a media server later attaches here: a `tee` after the caps,
/// with an encoder branch beside the application sink.
QString libcameraPipeline(const LibcameraPipelineConfig &config);

/// Captures from one libcamera device through GStreamer's `libcamerasrc`.
///
/// GStreamer is opened at run time, so the binary carries no dependency on it
/// and a machine without it starts as usual and simply lists no such camera.
///
/// Frames arrive on a GStreamer streaming thread and only the newest is kept.
/// `frameReady` is emitted on the thread this object lives on, at most once
/// per frame the consumer has not yet been told about, and the conversion to
/// an image happens in `takeFrame`, so a frame the consumer skips costs a
/// buffer reference and nothing else.
class LibcameraCapture : public QObject
{
    Q_OBJECT

public:
    explicit LibcameraCapture(QObject *parent = nullptr);
    ~LibcameraCapture() override;

    /// Opens `device` at the size chosen for `requested` and starts it.
    /// Returns false, with `errorString` set, when the pipeline cannot be
    /// built; failures after that arrive through `errorOccurred`.
    bool start(const LibcameraDevice &device, const QSize &requested);
    void stop();

    /// Whether a pipeline is running, delivering or not.
    bool isRunning() const { return m_pipeline != nullptr; }
    /// Whether a frame has arrived since the last start.
    bool hasDelivered() const { return m_delivered; }
    QString errorString() const { return m_errorString; }

    /// The newest frame as RGB888, upright and unmirrored, or a null image
    /// when there is none or its buffer does not hold what its caps describe.
    /// The second case also sets `errorString`, and a readable frame clears it.
    QImage takeFrame();

    /// The same in two steps, for a consumer that may not need the colour
    /// image at all. `holdLatest` takes the newest frame, releasing the one
    /// held before, and returns false when there is none. `heldLumaThumbnail`
    /// is `MotionGate::thumbnail` of its luma plane, which costs no colour
    /// conversion; `heldFrame` converts it as `takeFrame` does. The frame stays
    /// held until the next `holdLatest`, `releaseHeld` or `stop`.
    bool holdLatest();
    QImage heldLumaThumbnail() const;
    QImage heldFrame();
    void releaseHeld();

signals:
    void frameReady();
    /// The first frame after a start.
    void started();
    void errorOccurred(const QString &message);

private:
    /// The appsink callback, which runs on a streaming thread.
    friend struct LibcameraCallbacks;

    void deliver();
    void pollBus();
    bool launch(const LibcameraPipelineConfig &config);
    void teardown();

    // GStreamer objects, kept as opaque pointers so this header stays free of
    // GStreamer and GLib, which are opened at run time.
    void *m_pipeline{nullptr};
    void *m_sink{nullptr};
    void *m_bus{nullptr};

    QMutex m_sampleMutex;
    void *m_latestSample{nullptr};
    /// Owned by the thread this object lives on; only `m_latestSample` is
    /// shared with the streaming thread.
    void *m_heldSample{nullptr};
    std::atomic<bool> m_notifyPending{false};

    LibcameraPipelineConfig m_config;
    /// Whether the running pipeline is the fallback without a forced sensor
    /// mode or colour space.
    bool m_fallback{false};
    bool m_delivered{false};
    QString m_errorString;
    QTimer m_busTimer;
};

} // namespace Kerfur

#endif // KERFUR_LIBCAMERACAPTURE_H
