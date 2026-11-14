// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "videoframeimage.h"

#include <QAbstractVideoBuffer>
#include <QTest>
#include <QVideoFrameFormat>

#include <memory>
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

} // namespace

/// How a camera frame becomes the picture every model and the preview see.
class TestVideoFrameImage : public QObject
{
    Q_OBJECT

private slots:
    void packedFrameIsReadDirectly();
    void mislabelledFrameIsRefused();
    void mirroredFrameIsConverted();
    void invalidFrameIsNull();
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

QTEST_GUILESS_MAIN(TestVideoFrameImage)

#include "tst_videoframeimage.moc"
