// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_CAMERAFRAMEVIEW_H
#define KERFUR_CAMERAFRAMEVIEW_H

#include <QImage>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRectF>

namespace Kerfur {

/// Draws the camera preview, and nothing else.
///
/// This exists instead of an `Image` because an `Image` cannot be handed a
/// picture: it can only be given somewhere to fetch one from. The way to give
/// it a live frame was a JPEG encode, a base64 of that, a data URL built from
/// the base64 and a decode at the other end -- per bound view, per frame, on
/// the thread that draws the face. It also meant the picture arrived through
/// the asynchronous loader, so `status` cycled through `Loading` several times
/// a second, and everything that consulted it flickered with it: the overlay
/// vanished between frames and the click target that opens the popup switched
/// itself off for the moments the click was most likely to land in.
///
/// Here the frame is a QImage, uploaded to a texture on the render thread. No
/// encode, no decode, no loading state, and the geometry it was painted into is
/// published so that an overlay can put a normalized box exactly where the
/// picture put the thing it describes.
class CameraFrameView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    /// The picture to draw. A null image draws nothing at all rather than
    /// keeping the last one: the camera stopping is a thing the panel has to be
    /// able to show.
    Q_PROPERTY(QImage frame READ frame WRITE setFrame NOTIFY frameChanged)

    /// Where inside this item the frame was actually painted, aspect preserved
    /// and centred. The overlay's whole coordinate system, and the reason it is
    /// published rather than recomputed in QML from a size the view does not
    /// have.
    Q_PROPERTY(QRectF paintedRect READ paintedRect NOTIFY paintedRectChanged)

    /// Whether there is a picture at all. Not a loading state -- there is no
    /// loading -- so it changes when the camera starts and when it stops, and
    /// at no other time.
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged)

public:
    explicit CameraFrameView(QQuickItem *parent = nullptr);

    QImage frame() const { return m_frame; }
    void setFrame(const QImage &frame);

    QRectF paintedRect() const { return m_paintedRect; }
    bool hasFrame() const { return !m_frame.isNull(); }

signals:
    void frameChanged();
    void paintedRectChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void updatePaintedRect();

    QImage m_frame;
    QRectF m_paintedRect;
    /// Set on the GUI thread when a new frame arrives, cleared on the render
    /// thread once it has been uploaded. The two never run at once: the scene
    /// graph synchronisation this is read in holds the GUI thread still.
    bool m_uploadPending{false};
};

} // namespace Kerfur

#endif // KERFUR_CAMERAFRAMEVIEW_H
