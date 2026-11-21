// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "libcameracapture.h"

#include <QTest>

using namespace Kerfur;

namespace {

const QSize imx708Full{4608, 2592};
const QSize imx708Binned{2304, 1296};

} // namespace

/// The parts of libcamera capture that need no camera: ids, the size and
/// sensor mode choice, and the pipeline description.
class TestLibcameraCapture : public QObject
{
    Q_OBJECT

private slots:
    void idsRoundTrip();
    void qcameraIdsAreNotLibcameraIds();

    void exactSizeIsKept();
    void coveringSizeIsSmallest();
    void largestWhenNothingCovers();
    void anySizeWhenNothingIsOffered();

    void binnedModeKeepsTheFieldOfViewForSmallOutputs();
    void fullModeForOutputsLargerThanBinned();
    void noModeWithoutAFullSize();

    void usefulSizesShareTheSensorAspect();
    void usefulSizesAlwaysHoldFullAndBinned();

    void pipelineNamesCameraModeAndCaps();
    void pipelineNeverCarriesAFrameRate();
    void fallbackPipelineLeavesModeAndColourToLibcamera();
};

void TestLibcameraCapture::idsRoundTrip()
{
    const QString name{QStringLiteral("/base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a")};
    const QByteArray id{libcameraCameraId(name)};
    QVERIFY(isLibcameraCameraId(id));
    QCOMPARE(libcameraNameFromId(id), name);
}

void TestLibcameraCapture::qcameraIdsAreNotLibcameraIds()
{
    QVERIFY(!isLibcameraCameraId("/dev/video0"));
    QVERIFY(libcameraNameFromId("/dev/video0").isEmpty());
}

void TestLibcameraCapture::exactSizeIsKept()
{
    QCOMPARE(chooseCaptureSize({imx708Full, QSize{1920, 1080}, QSize{1280, 720}}, QSize{1920, 1080}),
             QSize(1920, 1080));
}

void TestLibcameraCapture::coveringSizeIsSmallest()
{
    QCOMPARE(chooseCaptureSize({imx708Full, imx708Binned, QSize{1280, 720}}, QSize{1920, 1080}),
             imx708Binned);
}

void TestLibcameraCapture::largestWhenNothingCovers()
{
    QCOMPARE(chooseCaptureSize({imx708Binned, QSize{1280, 720}}, QSize{8000, 6000}), imx708Binned);
}

void TestLibcameraCapture::anySizeWhenNothingIsOffered()
{
    QCOMPARE(chooseCaptureSize({}, QSize{1920, 1080}), QSize(1920, 1080));
}

void TestLibcameraCapture::binnedModeKeepsTheFieldOfViewForSmallOutputs()
{
    // Left alone, libcamera picks the IMX708's 1536x864 mode for this output,
    // a centre crop.
    QCOMPARE(fullViewSensorMode(imx708Full, QSize{1536, 864}), imx708Binned);
    QCOMPARE(fullViewSensorMode(imx708Full, imx708Binned), imx708Binned);
}

void TestLibcameraCapture::fullModeForOutputsLargerThanBinned()
{
    QCOMPARE(fullViewSensorMode(imx708Full, QSize{2560, 1440}), imx708Full);
    QCOMPARE(fullViewSensorMode(imx708Full, imx708Full), imx708Full);
}

void TestLibcameraCapture::noModeWithoutAFullSize()
{
    QVERIFY(fullViewSensorMode(QSize{}, QSize{1920, 1080}).isEmpty());
}

void TestLibcameraCapture::usefulSizesShareTheSensorAspect()
{
    const QList<QSize> offered{QSize{320, 180},
                               QSize{640, 360},
                               QSize{640, 480},
                               QSize{1280, 720},
                               QSize{1920, 1080},
                               QSize{1600, 1200},
                               imx708Full};
    const QList<QSize> useful{usefulCaptureSizes(offered, imx708Full)};
    QCOMPARE(useful,
             (QList<QSize>{imx708Full,
                           imx708Binned,
                           QSize{1920, 1080},
                           QSize{1280, 720},
                           QSize{640, 360}}));
}

void TestLibcameraCapture::usefulSizesAlwaysHoldFullAndBinned()
{
    const QList<QSize> useful{usefulCaptureSizes({}, imx708Full)};
    QCOMPARE(useful, (QList<QSize>{imx708Full, imx708Binned}));
    QVERIFY(usefulCaptureSizes({QSize{640, 360}}, QSize{}).isEmpty());
}

void TestLibcameraCapture::pipelineNamesCameraModeAndCaps()
{
    const QString pipeline{libcameraPipeline(LibcameraPipelineConfig{
        QStringLiteral("/base/imx708@1a"), QSize{1920, 1080}, imx708Binned, 10, true})};
    QVERIFY(pipeline.startsWith(QStringLiteral("libcamerasrc camera-name=\"/base/imx708@1a\"")));
    QVERIFY(pipeline.contains(
        QStringLiteral("sensor-config=\"sensor/config,width=2304,height=1296,depth=10\"")));
    // PiSP refuses a stream whose format it was not told.
    QVERIFY(pipeline.contains(
        QStringLiteral("video/x-raw,format=NV12,width=1920,height=1080,colorimetry=bt601")));
    QVERIFY(pipeline.contains(QStringLiteral("appsink name=sink max-buffers=1 drop=true")));
}

void TestLibcameraCapture::pipelineNeverCarriesAFrameRate()
{
    // Thirty a second asked of the IMX708's full mode stalls the pipeline
    // without an error.
    const QString pipeline{libcameraPipeline(
        LibcameraPipelineConfig{QStringLiteral("cam"), imx708Full, imx708Full, 10, true})};
    QVERIFY(!pipeline.contains(QStringLiteral("framerate")));
}

void TestLibcameraCapture::fallbackPipelineLeavesModeAndColourToLibcamera()
{
    const QString pipeline{libcameraPipeline(
        LibcameraPipelineConfig{QStringLiteral("cam"), QSize{1920, 1080}, QSize{}, 10, false})};
    QVERIFY(!pipeline.contains(QStringLiteral("sensor-config")));
    QVERIFY(!pipeline.contains(QStringLiteral("colorimetry")));
}

QTEST_APPLESS_MAIN(TestLibcameraCapture)

#include "tst_libcameracapture.moc"
