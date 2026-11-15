// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_BGRIMAGE_H
#define KERFUR_BGRIMAGE_H

#include <QImage>

#include <opencv2/core.hpp>

namespace Kerfur {

/// The image as an 8 bit, three channel BGR matrix that owns its pixels, or an
/// empty matrix for a null image. Alpha is dropped, not composited.
cv::Mat qImageToBgr(const QImage &image);

} // namespace Kerfur

#endif // KERFUR_BGRIMAGE_H
