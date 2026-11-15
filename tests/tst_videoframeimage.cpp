// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "videoframeimage.h"

#include <QAbstractVideoBuffer>
#include <QTest>
#include <QVideoFrameFormat>

#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

namespace {

/// A frame filled one pixel at a time, through the frame's own allocation.
QVideoFrame packedFrame(const QSize &size, const QList<QRgb> &pixels, bool mirrored)
{
    QVideoFrameFormat format{size, QVideoFrameFormat::Format_RGBA8888};
    format.setMirrored(mirrored);
    QVideoFrame frame{format};
    if (!frame.map(QVideoFrame::WriteOnly)) {
        return {};
    }
    for (int y{0}; y < size.height(); ++y) {
        uchar *row{frame.bits(0) + static_cast<qsizetype>(y) * frame.bytesPerLine(0)};
        for (int x{0}; x < size.width(); ++x) {
            const QRgb pixel{pixels.at(y * size.width() + x)};
            row[(x * 4) + 0] = static_cast<uchar>(qRed(pixel));
            row[(x * 4) + 1] = static_cast<uchar>(qGreen(pixel));
            row[(x * 4) + 2] = static_cast<uchar>(qBlue(pixel));
            row[(x * 4) + 3] = static_cast<uchar>(qAlpha(pixel));
        }
    }
    frame.unmap();
    return frame;
}

/// One plane of single byte rows labelled RGBA: what arrives when the luma
/// plane of a YUV frame is handed over under a packed format's name.
class MislabelledBuffer : public QAbstractVideoBuffer
{
public:
    explicit MislabelledBuffer(const QSize &size)
        : m_size{size}
        , m_bytes(static_cast<std::size_t>(size.width()) * static_cast<std::size_t>(size.height()),
                  uchar{0x80})
    {
    }

    MapData map(QVideoFrame::MapMode mode) override
    {
        Q_UNUSED(mode)
        MapData data;
        data.planeCount = 1;
        data.bytesPerLine[0] = m_size.width();
        data.data[0] = m_bytes.data();
        data.dataSize[0] = static_cast<int>(m_bytes.size());
        return data;
    }

    QVideoFrameFormat format() const override
    {
        return QVideoFrameFormat{m_size, QVideoFrameFormat::Format_RGBA8888};
    }

private:
    QSize m_size;
    std::vector<uchar> m_bytes;
};

/// A YUYV frame of which only the first `rows` rows arrived: what a camera
/// that delivers part of each frame hands over, size and stride intact.
class TruncatedYuyvBuffer : public QAbstractVideoBuffer
{
public:
    TruncatedYuyvBuffer(const QSize &size, int rows)
        : m_size{size}
        , m_bytes(static_cast<std::size_t>(size.width()) * 2U * static_cast<std::size_t>(rows),
                  uchar{0x80})
    {
    }

    MapData map(QVideoFrame::MapMode mode) override
    {
        Q_UNUSED(mode)
        MapData data;
        data.planeCount = 1;
        data.bytesPerLine[0] = m_size.width() * 2;
        data.data[0] = m_bytes.data();
        data.dataSize[0] = static_cast<int>(m_bytes.size());
        return data;
    }

    QVideoFrameFormat format() const override
    {
        return QVideoFrameFormat{m_size, QVideoFrameFormat::Format_YUYV};
    }

private:
    QSize m_size;
    std::vector<uchar> m_bytes;
};

/// Bytes as a browser hands them over: any layout, labelled RGBA, with the
/// first plane stride `copyTo` reported.
class BrowserBuffer : public QAbstractVideoBuffer
{
public:
    BrowserBuffer(const QSize &size, int stride, std::vector<uchar> bytes)
        : m_size{size}
        , m_stride{stride}
        , m_bytes{std::move(bytes)}
    {
    }

    MapData map(QVideoFrame::MapMode mode) override
    {
        Q_UNUSED(mode)
        MapData data;
        data.planeCount = 1;
        data.bytesPerLine[0] = m_stride;
        data.data[0] = m_bytes.data();
        data.dataSize[0] = static_cast<int>(m_bytes.size());
        return data;
    }

    QVideoFrameFormat format() const override
    {
        return QVideoFrameFormat{m_size, QVideoFrameFormat::Format_RGBA8888};
    }

private:
    QSize m_size;
    int m_stride{0};
    std::vector<uchar> m_bytes;
};

/// Within two levels on every channel: the fixed point arithmetic rounds, and
/// a reference colour quantised to 8 bit YUV does not come back exact.
bool closeTo(QRgb actual, QRgb expected)
{
    const auto near{[](int a, int b) { return std::abs(a - b) <= 2; }};
    return near(qRed(actual), qRed(expected)) && near(qGreen(actual), qGreen(expected))
           && near(qBlue(actual), qBlue(expected)) && qAlpha(actual) == 255;
}

} // namespace

/// How a camera frame becomes the picture every model and the preview see.
class TestVideoFrameImage : public QObject
{
    Q_OBJECT

private slots:
    void packedFrameIsReadDirectly();
    void mislabelledFrameIsRefused();
    void truncatedFrameIsRefused();
    void completeYuyvFrameIsConverted();
    void mirroredFrameIsConverted();
    void invalidFrameIsNull();
    void browserI420FrameIsDecoded();
    void browserNv12FrameIsDecoded();
    void browserBt709FrameIsDecoded();
    void browserBgrxFrameIsSwapped();
    void browserFrameShorterThanItsLayoutIsRefused();
};

void TestVideoFrameImage::packedFrameIsReadDirectly()
{
    const QList<QRgb> pixels{qRgba(255, 0, 0, 255),
                             qRgba(0, 255, 0, 255),
                             qRgba(0, 0, 255, 255),
                             qRgba(10, 20, 30, 255),
                             qRgba(40, 50, 60, 255),
                             qRgba(70, 80, 90, 255)};
    const QVideoFrame frame{packedFrame(QSize{3, 2}, pixels, false)};
    QVERIFY(frame.isValid());

    const QImage image{Kerfur::imageFromVideoFrame(frame)};

    QCOMPARE(image.size(), QSize(3, 2));
    // The frame's own format, untouched. The converter hands back a format of
    // its choosing, so this is what says the bytes were read and not converted.
    QCOMPARE(image.format(), QImage::Format_RGBA8888);
    for (int index{0}; index < pixels.size(); ++index) {
        QCOMPARE(image.pixel(index % 3, index / 3), pixels.at(index));
    }
}

void TestVideoFrameImage::mislabelledFrameIsRefused()
{
    const QVideoFrame frame{std::make_unique<MislabelledBuffer>(QSize{16, 8})};
    QVERIFY(frame.isValid());

    QVERIFY(Kerfur::imageFromVideoFrame(frame).isNull());
}

void TestVideoFrameImage::truncatedFrameIsRefused()
{
    const QVideoFrame frame{std::make_unique<TruncatedYuyvBuffer>(QSize{16, 8}, 2)};
    QVERIFY(frame.isValid());

    const Kerfur::PlaneFill fill{Kerfur::firstPlaneFill(frame)};
    QCOMPARE(fill.held, qsizetype{64});
    QCOMPARE(fill.needed, qsizetype{256});
    QVERIFY(Kerfur::imageFromVideoFrame(frame).isNull());
}

void TestVideoFrameImage::completeYuyvFrameIsConverted()
{
    const QVideoFrame frame{std::make_unique<TruncatedYuyvBuffer>(QSize{16, 8}, 8)};
    QVERIFY(frame.isValid());

    const Kerfur::PlaneFill fill{Kerfur::firstPlaneFill(frame)};
    QCOMPARE(fill.held, fill.needed);
    QCOMPARE(Kerfur::imageFromVideoFrame(frame).size(), QSize(16, 8));
}

void TestVideoFrameImage::mirroredFrameIsConverted()
{
    const QList<QRgb> pixels{qRgba(255, 0, 0, 255), qRgba(0, 0, 255, 255)};
    const QVideoFrame frame{packedFrame(QSize{2, 1}, pixels, true)};
    QVERIFY(frame.isValid());

    const QImage image{Kerfur::imageFromVideoFrame(frame)};

    // The format's flip is applied, which only the converter knows how to do.
    QCOMPARE(image.size(), QSize(2, 1));
    QCOMPARE(image.pixel(0, 0), pixels.at(1));
    QCOMPARE(image.pixel(1, 0), pixels.at(0));
}

void TestVideoFrameImage::invalidFrameIsNull()
{
    QVERIFY(Kerfur::imageFromVideoFrame(QVideoFrame{}).isNull());
}

void TestVideoFrameImage::browserI420FrameIsDecoded()
{
    // A red 2x2 block beside a green one, in limited range BT.601: the Y
    // plane, then U (90, 54), then V (240, 34).
    std::vector<uchar> bytes{81, 81, 145, 145, 81, 81, 145, 145, 90, 54, 240, 34};
    const QVideoFrame frame{std::make_unique<BrowserBuffer>(QSize{4, 2}, 4, std::move(bytes))};

    const QImage image{
        Kerfur::imageFromBrowserFrame(frame, {Kerfur::BrowserFrameLayout::Format::I420})};

    QCOMPARE(image.size(), QSize(4, 2));
    QVERIFY(closeTo(image.pixel(0, 0), qRgb(255, 0, 0)));
    QVERIFY(closeTo(image.pixel(1, 1), qRgb(255, 0, 0)));
    QVERIFY(closeTo(image.pixel(2, 0), qRgb(0, 255, 0)));
    QVERIFY(closeTo(image.pixel(3, 1), qRgb(0, 255, 0)));
}

void TestVideoFrameImage::browserNv12FrameIsDecoded()
{
    // The same red and green blocks, with U and V interleaved: (90, 240) for
    // red, (54, 34) for green.
    std::vector<uchar> bytes{81, 81, 145, 145, 81, 81, 145, 145, 90, 240, 54, 34};
    const QVideoFrame frame{std::make_unique<BrowserBuffer>(QSize{4, 2}, 4, std::move(bytes))};

    const QImage image{
        Kerfur::imageFromBrowserFrame(frame, {Kerfur::BrowserFrameLayout::Format::Nv12})};

    QCOMPARE(image.size(), QSize(4, 2));
    QVERIFY(closeTo(image.pixel(0, 0), qRgb(255, 0, 0)));
    QVERIFY(closeTo(image.pixel(3, 1), qRgb(0, 255, 0)));
}

void TestVideoFrameImage::browserBt709FrameIsDecoded()
{
    // Red in limited range BT.709 is (63, 102, 240); read as BT.601 it would
    // come back orange.
    std::vector<uchar> bytes{63, 63, 63, 63, 102, 240};
    const QVideoFrame frame{std::make_unique<BrowserBuffer>(QSize{2, 2}, 2, std::move(bytes))};

    const QImage image{Kerfur::imageFromBrowserFrame(
        frame, {Kerfur::BrowserFrameLayout::Format::I420, true, false})};

    QCOMPARE(image.size(), QSize(2, 2));
    QVERIFY(closeTo(image.pixel(1, 1), qRgb(255, 0, 0)));
}

void TestVideoFrameImage::browserBgrxFrameIsSwapped()
{
    // Red then blue, in BGRX order with junk in the unused byte.
    std::vector<uchar> bytes{0, 0, 255, 0, 255, 0, 0, 7};
    const QVideoFrame frame{std::make_unique<BrowserBuffer>(QSize{2, 1}, 8, std::move(bytes))};

    const QImage image{
        Kerfur::imageFromBrowserFrame(frame, {Kerfur::BrowserFrameLayout::Format::Bgrx})};

    QCOMPARE(image.size(), QSize(2, 1));
    QCOMPARE(image.pixel(0, 0), qRgb(255, 0, 0));
    QCOMPARE(image.pixel(1, 0), qRgb(0, 0, 255));
}

void TestVideoFrameImage::browserFrameShorterThanItsLayoutIsRefused()
{
    // The luma plane alone: I420 needs half as much again behind it.
    const QVideoFrame frame{std::make_unique<MislabelledBuffer>(QSize{16, 8})};

    QVERIFY(
        Kerfur::imageFromBrowserFrame(frame, {Kerfur::BrowserFrameLayout::Format::I420}).isNull());
    QVERIFY(Kerfur::imageFromBrowserFrame(frame, {}).isNull());
}

QTEST_GUILESS_MAIN(TestVideoFrameImage)

#include "tst_videoframeimage.moc"
