// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "cameraframeview.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>

namespace Kerfur {

CameraFrameView::CameraFrameView(QQuickItem *parent) : QQuickItem{parent}
{
    setFlag(ItemHasContents, true);
}

void CameraFrameView::setFrame(const QImage &frame)
{
    // Compared by identity, not by pixels. Two captures of the same still scene
    // are different frames and both have to be drawn; comparing the contents
    // would cost a full scan per frame to answer a question nobody asked.
    if (m_frame.cacheKey() == frame.cacheKey()) {
        return;
    }

    const QSize previous{m_frame.size()};
    m_frame = frame;
    m_uploadPending = true;
    if (m_frame.size() != previous) {
        updatePaintedRect();
    }
    update();
    emit frameChanged();
}

void CameraFrameView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        updatePaintedRect();
        update();
    }
}

void CameraFrameView::updatePaintedRect()
{
    QRectF painted;
    if (!m_frame.isNull() && width() > 0.0 && height() > 0.0) {
        // Aspect preserved and centred, which is what `Image.PreserveAspectFit`
        // did and what every normalized coordinate in the analysis is relative
        // to.
        const QSizeF scaled{QSizeF{m_frame.size()}.scaled(size(), Qt::KeepAspectRatio)};
        painted = QRectF{QPointF{(width() - scaled.width()) / 2.0,
                                 (height() - scaled.height()) / 2.0},
                         scaled};
    }

    if (painted == m_paintedRect) {
        return;
    }
    m_paintedRect = painted;
    emit paintedRectChanged();
}

QSGNode *CameraFrameView::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    if (m_frame.isNull() || m_paintedRect.isEmpty() || window() == nullptr) {
        delete node;
        return nullptr;
    }

    auto *textureNode{static_cast<QSGSimpleTextureNode *>(node)};
    if (textureNode == nullptr) {
        textureNode = new QSGSimpleTextureNode;
        textureNode->setOwnsTexture(true);
        textureNode->setFiltering(QSGTexture::Linear);
    }

    if (m_uploadPending || textureNode->texture() == nullptr) {
        m_uploadPending = false;
        // Owning the texture, so this deletes the previous one. The atlas is
        // allowed: a preview is small, and batching it with the overlay's
        // rectangles is the difference between one draw call and a dozen.
        textureNode->setTexture(
            window()->createTextureFromImage(m_frame, QQuickWindow::TextureCanUseAtlas));
    }

    textureNode->setRect(m_paintedRect);
    return textureNode;
}

} // namespace Kerfur
