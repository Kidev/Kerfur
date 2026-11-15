// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "bgrimage.h"

namespace Kerfur {

cv::Mat qImageToBgr(const QImage &image)
{
    if (image.isNull()) {
        return {};
    }

    // Format_BGR888 rather than a convert to RGB followed by a cvtColor: the
    // conversion has to happen anyway, and asking Qt for the byte order OpenCV
    // already wants makes it one pass instead of two.
    const QImage source{image.format() == QImage::Format_BGR888
                            ? image
                            : image.convertToFormat(QImage::Format_BGR888)};
    if (source.isNull()) {
        return {};
    }

    const cv::Mat view{source.height(),
                       source.width(),
                       CV_8UC3,
                       const_cast<uchar *>(source.constBits()),
                       static_cast<size_t>(source.bytesPerLine())};
    // Cloned because `source` may be the caller's implicitly shared buffer and
    // will be gone before the stages that read this are done with it.
    return view.clone();
}

} // namespace Kerfur
