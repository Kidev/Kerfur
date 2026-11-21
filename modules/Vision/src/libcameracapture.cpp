// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

// GLib and GStreamer before any Qt header: GLib names struct members
// `signals`, which Qt defines as a macro.
#ifdef KERFUR_HAS_LIBCAMERA
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#endif

#include "libcameracapture.h"

#include "motiongate.h"

#include <QLibrary>
#include <QLoggingCategory>
#include <QMutexLocker>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <utility>

namespace Kerfur {

namespace {

constexpr QByteArrayView idPrefix{"libcamera:"};

qint64 area(const QSize &size)
{
    return static_cast<qint64>(size.width()) * size.height();
}

} // namespace

QByteArray libcameraCameraId(const QString &name)
{
    return idPrefix.toByteArray() + name.toUtf8();
}

bool isLibcameraCameraId(const QByteArray &id)
{
    return id.startsWith(idPrefix);
}

QString libcameraNameFromId(const QByteArray &id)
{
    return isLibcameraCameraId(id) ? QString::fromUtf8(id.mid(idPrefix.size())) : QString{};
}

QSize chooseCaptureSize(const QList<QSize> &offered, const QSize &requested)
{
    if (offered.isEmpty() || offered.contains(requested)) {
        return requested;
    }
    QSize covering;
    QSize largest;
    for (const QSize &size : offered) {
        if (size.width() >= requested.width() && size.height() >= requested.height() &&
            (covering.isEmpty() || area(size) < area(covering))) {
            covering = size;
        }
        if (largest.isEmpty() || area(size) > area(largest)) {
            largest = size;
        }
    }
    return covering.isEmpty() ? largest : covering;
}

QSize fullViewSensorMode(const QSize &fullSize, const QSize &output)
{
    if (fullSize.isEmpty()) {
        return {};
    }
    const QSize binned{fullSize.width() / 2, fullSize.height() / 2};
    return output.width() <= binned.width() && output.height() <= binned.height() ? binned
                                                                                  : fullSize;
}

QList<QSize> usefulCaptureSizes(const QList<QSize> &offered, const QSize &fullSize)
{
    if (fullSize.isEmpty()) {
        return {};
    }
    QList<QSize> sizes{fullSize, QSize{fullSize.width() / 2, fullSize.height() / 2}};
    for (const QSize &size : offered) {
        // Same aspect within one percent, compared without division.
        const qint64 crossed{std::abs(static_cast<qint64>(size.width()) * fullSize.height() -
                                      static_cast<qint64>(size.height()) * fullSize.width())};
        const bool sameAspect{crossed * 100 <=
                              static_cast<qint64>(size.width()) * fullSize.height()};
        if (sameAspect && size.width() >= minimumOfferedWidth && size.width() <= fullSize.width() &&
            size.height() <= fullSize.height() && !sizes.contains(size)) {
            sizes.append(size);
        }
    }
    std::sort(sizes.begin(), sizes.end(), [](const QSize &left, const QSize &right) {
        return area(left) > area(right);
    });
    return sizes;
}

QString libcameraPipeline(const LibcameraPipelineConfig &config)
{
    QString source{QStringLiteral("libcamerasrc camera-name=\"%1\"").arg(config.cameraName)};
    if (!config.sensorMode.isEmpty()) {
        source += QStringLiteral(" sensor-config=\"sensor/config,width=%1,height=%2,depth=%3\"")
                      .arg(config.sensorMode.width())
                      .arg(config.sensorMode.height())
                      .arg(config.sensorDepth);
    }
    QString caps{QStringLiteral("video/x-raw,format=NV12,width=%1,height=%2")
                     .arg(config.output.width())
                     .arg(config.output.height())};
    if (config.bt601) {
        caps += QStringLiteral(",colorimetry=bt601");
    }
    // One buffer, the newest: a consumer that reads a few frames a second
    // must never hold the camera's buffer pool back.
    return source + QStringLiteral(" ! ") + caps +
           QStringLiteral(" ! appsink name=sink max-buffers=1 drop=true sync=false "
                          "enable-last-sample=false");
}

#ifdef KERFUR_HAS_LIBCAMERA

namespace {

Q_LOGGING_CATEGORY(libcameraLog, "kerfur.vision.libcamera")

/// The GStreamer and GLib entry points this file calls, resolved from the
/// system's libraries at first use. Declared with `decltype` of the real
/// prototypes, so the headers still check every call.
struct GstApi
{
    bool loaded{false};

    decltype(&::gst_init_check) initCheck{nullptr};
    decltype(&::gst_parse_launch) parseLaunch{nullptr};
    decltype(&::gst_object_ref_sink) objectRefSink{nullptr};
    decltype(&::gst_object_unref) objectUnref{nullptr};
    decltype(&::gst_bin_get_by_name) binGetByName{nullptr};
    decltype(&::gst_element_set_state) elementSetState{nullptr};
    decltype(&::gst_element_get_bus) elementGetBus{nullptr};
    decltype(&::gst_bus_pop_filtered) busPopFiltered{nullptr};
    decltype(&::gst_message_parse_error) messageParseError{nullptr};
    decltype(&::gst_mini_object_unref) miniObjectUnref{nullptr};
    decltype(&::gst_sample_get_buffer) sampleGetBuffer{nullptr};
    decltype(&::gst_sample_get_caps) sampleGetCaps{nullptr};
    decltype(&::gst_buffer_map) bufferMap{nullptr};
    decltype(&::gst_buffer_unmap) bufferUnmap{nullptr};
    decltype(&::gst_buffer_get_meta) bufferGetMeta{nullptr};
    decltype(&::gst_device_provider_factory_get_by_name) providerByName{nullptr};
    decltype(&::gst_device_provider_get_devices) providerGetDevices{nullptr};
    decltype(&::gst_device_get_display_name) deviceGetDisplayName{nullptr};
    decltype(&::gst_device_get_properties) deviceGetProperties{nullptr};
    decltype(&::gst_device_get_caps) deviceGetCaps{nullptr};
    decltype(&::gst_caps_get_size) capsGetSize{nullptr};
    decltype(&::gst_caps_get_structure) capsGetStructure{nullptr};
    decltype(&::gst_structure_has_name) structureHasName{nullptr};
    decltype(&::gst_structure_get_string) structureGetString{nullptr};
    decltype(&::gst_structure_get_int) structureGetInt{nullptr};
    decltype(&::gst_structure_free) structureFree{nullptr};
    decltype(&::gst_structure_get_value) structureGetValue{nullptr};
    decltype(&::gst_value_array_get_size) valueArrayGetSize{nullptr};
    decltype(&::gst_value_array_get_value) valueArrayGetValue{nullptr};
    decltype(&::gst_app_sink_set_callbacks) appSinkSetCallbacks{nullptr};
    decltype(&::gst_app_sink_pull_sample) appSinkPullSample{nullptr};
    decltype(&::gst_video_info_from_caps) videoInfoFromCaps{nullptr};
    decltype(&::gst_video_meta_api_get_type) videoMetaApiGetType{nullptr};
    decltype(&::g_free) gFree{nullptr};
    decltype(&::g_error_free) errorFree{nullptr};
    decltype(&::g_list_free) listFree{nullptr};
    decltype(&::g_value_get_int) valueGetInt{nullptr};
    decltype(&::g_type_check_value_holds) valueHolds{nullptr};
};

template<typename Function>
bool resolve(QLibrary &library, const char *symbol, Function &target)
{
    target = reinterpret_cast<Function>(library.resolve(symbol));
    if (target == nullptr) {
        qCWarning(libcameraLog) << "GStreamer has no" << symbol << "in" << library.fileName();
    }
    return target != nullptr;
}

GstApi loadGstApi()
{
    GstApi api;
    // Never unloaded: QLibrary keeps a library mapped after its object goes,
    // which is what every pointer resolved below relies on.
    QLibrary glib{QStringLiteral("glib-2.0"), 0};
    QLibrary gobject{QStringLiteral("gobject-2.0"), 0};
    QLibrary core{QStringLiteral("gstreamer-1.0"), 0};
    QLibrary app{QStringLiteral("gstapp-1.0"), 0};
    QLibrary video{QStringLiteral("gstvideo-1.0"), 0};
    for (QLibrary *library : {&glib, &gobject, &core, &app, &video}) {
        if (!library->load()) {
            qCInfo(libcameraLog) << "libcamera capture unavailable:" << library->errorString();
            return api;
        }
    }

    const bool resolved{
        resolve(core, "gst_init_check", api.initCheck) &&
        resolve(core, "gst_parse_launch", api.parseLaunch) &&
        resolve(core, "gst_object_ref_sink", api.objectRefSink) &&
        resolve(core, "gst_object_unref", api.objectUnref) &&
        resolve(core, "gst_bin_get_by_name", api.binGetByName) &&
        resolve(core, "gst_element_set_state", api.elementSetState) &&
        resolve(core, "gst_element_get_bus", api.elementGetBus) &&
        resolve(core, "gst_bus_pop_filtered", api.busPopFiltered) &&
        resolve(core, "gst_message_parse_error", api.messageParseError) &&
        resolve(core, "gst_mini_object_unref", api.miniObjectUnref) &&
        resolve(core, "gst_sample_get_buffer", api.sampleGetBuffer) &&
        resolve(core, "gst_sample_get_caps", api.sampleGetCaps) &&
        resolve(core, "gst_buffer_map", api.bufferMap) &&
        resolve(core, "gst_buffer_unmap", api.bufferUnmap) &&
        resolve(core, "gst_buffer_get_meta", api.bufferGetMeta) &&
        resolve(core, "gst_device_provider_factory_get_by_name", api.providerByName) &&
        resolve(core, "gst_device_provider_get_devices", api.providerGetDevices) &&
        resolve(core, "gst_device_get_display_name", api.deviceGetDisplayName) &&
        resolve(core, "gst_device_get_properties", api.deviceGetProperties) &&
        resolve(core, "gst_device_get_caps", api.deviceGetCaps) &&
        resolve(core, "gst_caps_get_size", api.capsGetSize) &&
        resolve(core, "gst_caps_get_structure", api.capsGetStructure) &&
        resolve(core, "gst_structure_has_name", api.structureHasName) &&
        resolve(core, "gst_structure_get_string", api.structureGetString) &&
        resolve(core, "gst_structure_get_int", api.structureGetInt) &&
        resolve(core, "gst_structure_free", api.structureFree) &&
        resolve(core, "gst_structure_get_value", api.structureGetValue) &&
        resolve(core, "gst_value_array_get_size", api.valueArrayGetSize) &&
        resolve(core, "gst_value_array_get_value", api.valueArrayGetValue) &&
        resolve(app, "gst_app_sink_set_callbacks", api.appSinkSetCallbacks) &&
        resolve(app, "gst_app_sink_pull_sample", api.appSinkPullSample) &&
        resolve(video, "gst_video_info_from_caps", api.videoInfoFromCaps) &&
        resolve(video, "gst_video_meta_api_get_type", api.videoMetaApiGetType) &&
        resolve(glib, "g_free", api.gFree) && resolve(glib, "g_error_free", api.errorFree) &&
        resolve(glib, "g_list_free", api.listFree) &&
        resolve(gobject, "g_value_get_int", api.valueGetInt) &&
        resolve(gobject, "g_type_check_value_holds", api.valueHolds)};
    if (!resolved) {
        return api;
    }

    GError *error{nullptr};
    if (api.initCheck(nullptr, nullptr, &error) == FALSE) {
        qCWarning(libcameraLog) << "GStreamer failed to initialise:"
                                << (error != nullptr ? error->message : "no reason given");
        if (error != nullptr) {
            api.errorFree(error);
        }
        return api;
    }
    api.loaded = true;
    return api;
}

const GstApi &gstApi()
{
    static const GstApi api{loadGstApi()};
    return api;
}

/// The sensor's active area from a libcamera device's properties, or an empty
/// size. The caps list output sizes only up to what the pipeline's standard
/// size table holds, which can stop short of the sensor (3840x2400 for a
/// 4608x2592 IMX708), so the full size is read from here.
QSize activeArea(const GstApi &api, const GstStructure *properties)
{
    const GValue *areas{api.structureGetValue(properties, "api.libcamera.PixelArrayActiveAreas")};
    if (areas == nullptr || api.valueArrayGetSize(areas) < 4) {
        return {};
    }
    std::array<int, 4> values{};
    for (guint index{0}; index < 4; ++index) {
        const GValue *value{api.valueArrayGetValue(areas, index)};
        if (value == nullptr || api.valueHolds(value, G_TYPE_INT) == FALSE) {
            return {};
        }
        values.at(index) = api.valueGetInt(value);
    }
    // x, y, width, height.
    return values[2] > 0 && values[3] > 0 ? QSize{values[2], values[3]} : QSize{};
}

void unrefMiniObject(const GstApi &api, void *object)
{
    if (object != nullptr) {
        api.miniObjectUnref(static_cast<GstMiniObject *>(object));
    }
}

/// Where the two planes of an NV12 sample lie in its buffer.
struct Nv12Layout
{
    int width{0};
    int height{0};
    std::array<gsize, 2> offset{};
    std::array<gint, 2> stride{};
};

/// The layout of `sample`, or nothing when it is not an NV12 frame this code
/// can read.
std::optional<Nv12Layout> nv12Layout(const GstApi &api, GstSample *sample)
{
    GstCaps *caps{api.sampleGetCaps(sample)};
    GstBuffer *buffer{api.sampleGetBuffer(sample)};
    GstVideoInfo info{};
    if (caps == nullptr || buffer == nullptr || api.videoInfoFromCaps(&info, caps) == FALSE ||
        info.finfo == nullptr || info.finfo->format != GST_VIDEO_FORMAT_NV12) {
        return std::nullopt;
    }

    Nv12Layout layout{info.width,
                      info.height,
                      {info.offset[0], info.offset[1]},
                      {info.stride[0], info.stride[1]}};
    // The pipeline pads its rows, and when it does it says so in the meta
    // rather than in the caps.
    GstMeta *meta{api.bufferGetMeta(buffer, api.videoMetaApiGetType())};
    if (meta != nullptr) {
        const auto *videoMeta{reinterpret_cast<const GstVideoMeta *>(meta)};
        if (videoMeta->n_planes >= 2) {
            layout.width = static_cast<int>(videoMeta->width);
            layout.height = static_cast<int>(videoMeta->height);
            layout.offset = {videoMeta->offset[0], videoMeta->offset[1]};
            layout.stride = {videoMeta->stride[0], videoMeta->stride[1]};
        }
    }
    if (layout.width <= 0 || layout.height <= 0 || (layout.width % 2) != 0 ||
        (layout.height % 2) != 0 || layout.stride[0] < layout.width ||
        layout.stride[1] < layout.width) {
        return std::nullopt;
    }
    return layout;
}

gsize lumaEnd(const Nv12Layout &layout)
{
    return layout.offset[0] +
           static_cast<gsize>(layout.stride[0]) * static_cast<gsize>(layout.height);
}

/// The NV12 frame in `sample` as RGB888, or a null image when the buffer does
/// not hold the planes its caps and video meta describe.
QImage imageFromNv12Sample(const GstApi &api, GstSample *sample)
{
    const std::optional<Nv12Layout> layout{nv12Layout(api, sample)};
    if (!layout.has_value()) {
        return {};
    }
    const int width{layout->width};
    const int height{layout->height};

    GstBuffer *buffer{api.sampleGetBuffer(sample)};
    GstMapInfo map{};
    if (api.bufferMap(buffer, &map, GST_MAP_READ) == FALSE) {
        return {};
    }
    const gsize chromaEnd{layout->offset[1] +
                          static_cast<gsize>(layout->stride[1]) * static_cast<gsize>(height / 2)};
    QImage image;
    if (map.size >= lumaEnd(*layout) && map.size >= chromaEnd) {
        const cv::Mat luma{height,
                           width,
                           CV_8UC1,
                           map.data + layout->offset[0],
                           static_cast<size_t>(layout->stride[0])};
        const cv::Mat chroma{height / 2,
                             width / 2,
                             CV_8UC2,
                             map.data + layout->offset[1],
                             static_cast<size_t>(layout->stride[1])};
        image = QImage{width, height, QImage::Format_RGB888};
        cv::Mat rgb{height, width, CV_8UC3, image.bits(), static_cast<size_t>(image.bytesPerLine())};
        try {
            cv::cvtColorTwoPlane(luma, chroma, rgb, cv::COLOR_YUV2RGB_NV12);
        } catch (const cv::Exception &error) {
            qCWarning(libcameraLog) << "NV12 conversion failed:" << error.what();
            image = QImage{};
        }
        // The conversion writes into the image only while the destination it
        // was handed is already the right size and type.
        if (!image.isNull() && rgb.data != image.bits()) {
            image = QImage{};
        }
    }
    api.bufferUnmap(buffer, &map);
    return image;
}

/// The motion thumbnail of `sample`, read from its luma plane alone: the
/// chroma plane is never touched and no colour conversion happens.
QImage lumaThumbnailOfNv12Sample(const GstApi &api, GstSample *sample)
{
    const std::optional<Nv12Layout> layout{nv12Layout(api, sample)};
    if (!layout.has_value()) {
        return {};
    }
    GstBuffer *buffer{api.sampleGetBuffer(sample)};
    GstMapInfo map{};
    if (api.bufferMap(buffer, &map, GST_MAP_READ) == FALSE) {
        return {};
    }
    QImage thumbnail;
    if (map.size >= lumaEnd(*layout)) {
        thumbnail = MotionGate::thumbnail(map.data + layout->offset[0],
                                          layout->width,
                                          layout->height,
                                          layout->stride[0]);
    }
    api.bufferUnmap(buffer, &map);
    return thumbnail;
}

} // namespace

struct LibcameraCallbacks
{
    static GstFlowReturn newSample(GstAppSink *sink, gpointer data)
    {
        auto *capture{static_cast<LibcameraCapture *>(data)};
        const GstApi &api{gstApi()};
        GstSample *sample{api.appSinkPullSample(sink)};
        if (sample == nullptr) {
            return GST_FLOW_OK;
        }
        void *previous{nullptr};
        {
            const QMutexLocker lock{&capture->m_sampleMutex};
            previous = capture->m_latestSample;
            capture->m_latestSample = sample;
        }
        unrefMiniObject(api, previous);
        // One notification per run of frames the consumer has not heard of.
        if (!capture->m_notifyPending.exchange(true)) {
            QMetaObject::invokeMethod(capture, &LibcameraCapture::deliver, Qt::QueuedConnection);
        }
        return GST_FLOW_OK;
    }
};

QList<LibcameraDevice> libcameraDevices()
{
    const GstApi &api{gstApi()};
    if (!api.loaded) {
        return {};
    }
    GstDeviceProvider *provider{api.providerByName("libcameraprovider")};
    if (provider == nullptr) {
        qCInfo(libcameraLog) << "no libcamera device provider (gstreamer1.0-libcamera)";
        return {};
    }

    QList<LibcameraDevice> devices;
    GList *found{api.providerGetDevices(provider)};
    for (GList *node{found}; node != nullptr; node = node->next) {
        auto *device{static_cast<GstDevice *>(node->data)};

        bool v4l2Capable{false};
        LibcameraDevice entry;
        QSize sensorArea;
        if (GstStructure * properties{api.deviceGetProperties(device)}) {
            sensorArea = activeArea(api, properties);
            const gchar *handler{
                api.structureGetString(properties, "api.libcamera.PipelineHandler")};
            const gchar *model{api.structureGetString(properties, "api.libcamera.Model")};
            v4l2Capable = handler != nullptr && QByteArrayView{handler} == "uvcvideo";
            entry.model = model != nullptr ? QString::fromUtf8(model) : QString{};
            api.structureFree(properties);
        }
        if (gchar * name{api.deviceGetDisplayName(device)}) {
            entry.name = QString::fromUtf8(name);
            api.gFree(name);
        }

        QList<QSize> offered;
        if (GstCaps * caps{api.deviceGetCaps(device)}) {
            const guint count{api.capsGetSize(caps)};
            for (guint index{0}; index < count; ++index) {
                const GstStructure *structure{api.capsGetStructure(caps, index)};
                const gchar *format{api.structureGetString(structure, "format")};
                gint width{0};
                gint height{0};
                if (api.structureHasName(structure, "video/x-raw") != FALSE && format != nullptr &&
                    QByteArrayView{format} == "NV12" &&
                    api.structureGetInt(structure, "width", &width) != FALSE &&
                    api.structureGetInt(structure, "height", &height) != FALSE) {
                    offered.append(QSize{width, height});
                }
            }
            unrefMiniObject(api, caps);
        }
        api.objectUnref(device);

        // The largest offered size stands in when the device does not say.
        QSize fullSize{sensorArea};
        if (fullSize.isEmpty()) {
            for (const QSize &size : std::as_const(offered)) {
                if (area(size) > area(fullSize)) {
                    fullSize = size;
                }
            }
        }
        entry.fullSize = fullSize;
        entry.sizes = usefulCaptureSizes(offered, fullSize);
        if (entry.model.isEmpty()) {
            entry.model = entry.name;
        }
        if (!v4l2Capable && !entry.name.isEmpty() && !entry.sizes.isEmpty()) {
            qCInfo(libcameraLog) << "found" << entry.model << "at" << entry.name << "full size"
                                 << entry.fullSize;
            devices.append(entry);
        }
    }
    api.listFree(found);
    api.objectUnref(provider);
    return devices;
}

LibcameraCapture::LibcameraCapture(QObject *parent) : QObject{parent}, m_busTimer{this}
{
    m_busTimer.setInterval(250);
    connect(&m_busTimer, &QTimer::timeout, this, &LibcameraCapture::pollBus);
}

LibcameraCapture::~LibcameraCapture()
{
    teardown();
}

bool LibcameraCapture::start(const LibcameraDevice &device, const QSize &requested)
{
    teardown();
    m_errorString.clear();
    if (!gstApi().loaded) {
        m_errorString = QStringLiteral("GStreamer is not installed");
        return false;
    }
    // The name goes into a pipeline description between double quotes.
    if (device.name.isEmpty() || device.name.contains(QLatin1Char('"')) ||
        device.name.contains(QLatin1Char('\\'))) {
        m_errorString = QStringLiteral("Camera name cannot be used: %1").arg(device.name);
        return false;
    }
    const QSize output{chooseCaptureSize(device.sizes, requested)};
    m_config = LibcameraPipelineConfig{device.name,
                                       output,
                                       fullViewSensorMode(device.fullSize, output),
                                       10,
                                       true};
    m_fallback = false;
    return launch(m_config);
}

bool LibcameraCapture::launch(const LibcameraPipelineConfig &config)
{
    const GstApi &api{gstApi()};
    const QByteArray description{libcameraPipeline(config).toUtf8()};

    GError *error{nullptr};
    GstElement *pipeline{api.parseLaunch(description.constData(), &error)};
    if (error != nullptr) {
        m_errorString =
            QStringLiteral("Camera pipeline rejected: %1").arg(QString::fromUtf8(error->message));
        api.errorFree(error);
        if (pipeline != nullptr) {
            api.objectUnref(pipeline);
        }
        return false;
    }
    if (pipeline == nullptr) {
        m_errorString = QStringLiteral("Camera pipeline could not be built");
        return false;
    }
    api.objectRefSink(pipeline);

    GstElement *sink{api.binGetByName(reinterpret_cast<GstBin *>(pipeline), "sink")};
    if (sink == nullptr) {
        api.objectUnref(pipeline);
        m_errorString = QStringLiteral("Camera pipeline has no sink");
        return false;
    }
    GstAppSinkCallbacks callbacks{};
    callbacks.new_sample = &LibcameraCallbacks::newSample;
    api.appSinkSetCallbacks(reinterpret_cast<GstAppSink *>(sink), &callbacks, this, nullptr);

    m_pipeline = pipeline;
    m_sink = sink;
    m_bus = api.elementGetBus(pipeline);
    m_delivered = false;

    if (api.elementSetState(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        teardown();
        m_errorString = QStringLiteral("Camera failed to start");
        return false;
    }
    m_busTimer.start();
    qCInfo(libcameraLog) << "capturing" << config.output << "sensor mode"
                         << (config.sensorMode.isEmpty() ? QStringLiteral("automatic")
                                                         : QStringLiteral("%1x%2")
                                                               .arg(config.sensorMode.width())
                                                               .arg(config.sensorMode.height()));
    return true;
}

void LibcameraCapture::stop()
{
    teardown();
}

void LibcameraCapture::teardown()
{
    m_busTimer.stop();
    if (m_pipeline == nullptr) {
        return;
    }
    const GstApi &api{gstApi()};
    // Blocks until the streaming threads have left, so no callback can run
    // after this.
    api.elementSetState(static_cast<GstElement *>(m_pipeline), GST_STATE_NULL);
    if (m_bus != nullptr) {
        api.objectUnref(m_bus);
    }
    api.objectUnref(m_sink);
    api.objectUnref(m_pipeline);
    m_pipeline = nullptr;
    m_sink = nullptr;
    m_bus = nullptr;
    m_delivered = false;

    void *sample{nullptr};
    {
        const QMutexLocker lock{&m_sampleMutex};
        sample = m_latestSample;
        m_latestSample = nullptr;
    }
    unrefMiniObject(api, sample);
    releaseHeld();
}

QImage LibcameraCapture::takeFrame()
{
    if (!holdLatest()) {
        return {};
    }
    QImage image{heldFrame()};
    releaseHeld();
    return image;
}

bool LibcameraCapture::holdLatest()
{
    void *sample{nullptr};
    {
        const QMutexLocker lock{&m_sampleMutex};
        sample = m_latestSample;
        m_latestSample = nullptr;
    }
    if (sample == nullptr) {
        return false;
    }
    releaseHeld();
    m_heldSample = sample;
    return true;
}

QImage LibcameraCapture::heldLumaThumbnail() const
{
    if (m_heldSample == nullptr) {
        return {};
    }
    return lumaThumbnailOfNv12Sample(gstApi(), static_cast<GstSample *>(m_heldSample));
}

QImage LibcameraCapture::heldFrame()
{
    if (m_heldSample == nullptr) {
        return {};
    }
    QImage image{imageFromNv12Sample(gstApi(), static_cast<GstSample *>(m_heldSample))};
    m_errorString = image.isNull()
                        ? QStringLiteral("Camera frames arrive in a layout that cannot be read")
                        : QString{};
    return image;
}

void LibcameraCapture::releaseHeld()
{
    if (m_heldSample == nullptr) {
        return;
    }
    unrefMiniObject(gstApi(), m_heldSample);
    m_heldSample = nullptr;
}

void LibcameraCapture::pollBus()
{
    if (m_bus == nullptr) {
        return;
    }
    const GstApi &api{gstApi()};
    GstMessage *message{
        api.busPopFiltered(static_cast<GstBus *>(m_bus),
                           static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))};
    if (message == nullptr) {
        return;
    }
    QString reason{QStringLiteral("The camera stream ended")};
    if (message->type == GST_MESSAGE_ERROR) {
        GError *error{nullptr};
        gchar *debug{nullptr};
        api.messageParseError(message, &error, &debug);
        if (error != nullptr) {
            reason = QString::fromUtf8(error->message);
            api.errorFree(error);
        }
        if (debug != nullptr) {
            qCDebug(libcameraLog) << debug;
            api.gFree(debug);
        }
    }
    unrefMiniObject(api, message);

    // A sensor that has no such binned mode, or no BT.601 output, refuses the
    // configuration before the first frame. Once, it gets the pipeline that
    // leaves both to libcamera.
    if (!m_delivered && !m_fallback) {
        qCWarning(libcameraLog) << "retrying with an automatic sensor mode:" << reason;
        teardown();
        m_fallback = true;
        LibcameraPipelineConfig fallback{m_config};
        fallback.sensorMode = QSize{};
        fallback.bt601 = false;
        if (launch(fallback)) {
            return;
        }
        reason = m_errorString;
    }
    teardown();
    m_errorString = reason;
    emit errorOccurred(reason);
}

void LibcameraCapture::deliver()
{
    m_notifyPending = false;
    if (m_pipeline == nullptr) {
        return;
    }
    bool hasSample{false};
    {
        const QMutexLocker lock{&m_sampleMutex};
        hasSample = m_latestSample != nullptr;
    }
    // A notification queued by a pipeline that has since been replaced finds
    // no sample, and says nothing.
    if (!hasSample) {
        return;
    }
    if (!m_delivered) {
        m_delivered = true;
        emit started();
    }
    emit frameReady();
}

#else // KERFUR_HAS_LIBCAMERA

QList<LibcameraDevice> libcameraDevices()
{
    return {};
}

LibcameraCapture::LibcameraCapture(QObject *parent) : QObject{parent}, m_busTimer{this} {}

LibcameraCapture::~LibcameraCapture() = default;

bool LibcameraCapture::start(const LibcameraDevice &, const QSize &)
{
    m_errorString = QStringLiteral("Kerfur was built without GStreamer");
    return false;
}

void LibcameraCapture::stop() {}

QImage LibcameraCapture::takeFrame()
{
    return {};
}

bool LibcameraCapture::holdLatest()
{
    return false;
}

QImage LibcameraCapture::heldLumaThumbnail() const
{
    return {};
}

QImage LibcameraCapture::heldFrame()
{
    return {};
}

void LibcameraCapture::releaseHeld() {}

void LibcameraCapture::deliver() {}
void LibcameraCapture::pollBus() {}
void LibcameraCapture::teardown() {}

bool LibcameraCapture::launch(const LibcameraPipelineConfig &)
{
    return false;
}

#endif // KERFUR_HAS_LIBCAMERA

} // namespace Kerfur
