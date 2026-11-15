// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "bgrimage.h"

#include <QTest>

/// How a decoded picture becomes the matrix the face stages read.
class TestBgrImage : public QObject
{
    Q_OBJECT

private slots:
    void channelsAreReordered();
    void alphaIsDropped();
    void matrixOwnsItsPixels();
    void nullImageIsEmpty();
};

void TestBgrImage::channelsAreReordered()
{
    QImage image{2, 1, QImage::Format_RGB888};
    image.setPixel(0, 0, qRgb(255, 10, 0));
    image.setPixel(1, 0, qRgb(0, 20, 255));

    const cv::Mat bgr{Kerfur::qImageToBgr(image)};

    QCOMPARE(bgr.type(), CV_8UC3);
    QCOMPARE(bgr.cols, 2);
    QCOMPARE(bgr.rows, 1);
    QCOMPARE(bgr.at<cv::Vec3b>(0, 0), cv::Vec3b(0, 10, 255));
    QCOMPARE(bgr.at<cv::Vec3b>(0, 1), cv::Vec3b(255, 20, 0));
}

void TestBgrImage::alphaIsDropped()
{
    QImage image{1, 1, QImage::Format_ARGB32};
    image.setPixel(0, 0, qRgba(40, 50, 60, 255));

    const cv::Mat bgr{Kerfur::qImageToBgr(image)};

    QCOMPARE(bgr.type(), CV_8UC3);
    QCOMPARE(bgr.at<cv::Vec3b>(0, 0), cv::Vec3b(60, 50, 40));
}

void TestBgrImage::matrixOwnsItsPixels()
{
    QImage image{1, 1, QImage::Format_BGR888};
    image.setPixel(0, 0, qRgb(1, 2, 3));

    const cv::Mat bgr{Kerfur::qImageToBgr(image)};
    image.setPixel(0, 0, qRgb(200, 200, 200));

    QCOMPARE(bgr.at<cv::Vec3b>(0, 0), cv::Vec3b(3, 2, 1));
}

void TestBgrImage::nullImageIsEmpty()
{
    QVERIFY(Kerfur::qImageToBgr(QImage{}).empty());
}

QTEST_GUILESS_MAIN(TestBgrImage)
#include "tst_bgrimage.moc"
