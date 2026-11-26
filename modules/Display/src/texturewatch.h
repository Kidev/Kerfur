// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef KERFUR_TEXTUREWATCH_H
#define KERFUR_TEXTUREWATCH_H

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSGTextureProvider>

namespace Kerfur {

/// Says when another item's texture is redrawn.
///
/// `source` is a texture provider: a ShaderEffectSource, or an item with a
/// layer. Its texture is redrawn whenever something in the subtree it captures
/// changes, and `textureChanged` follows on the GUI thread. While the subtree
/// stays the same nothing is emitted, so a consumer can run a clock only while
/// the picture changes.
///
/// The watch draws nothing and needs no size, but it has to be in the same
/// window as `source` and visible: it hooks onto the texture while the scene
/// graph synchronises it.
class TextureWatch : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    /// The item whose texture is watched. An item that provides no texture
    /// never reports a change.
    Q_PROPERTY(QQuickItem *source READ source WRITE setSource NOTIFY sourceChanged)

public:
    explicit TextureWatch(QQuickItem *parent = nullptr);

    QQuickItem *source() const { return m_source; }
    void setSource(QQuickItem *source);

signals:
    void sourceChanged();
    /// The texture of `source` was redrawn.
    void textureChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    QPointer<QQuickItem> m_source;
    QMetaObject::Connection m_sourceDestroyed;
    /// The provider `m_connection` listens to. Touched only while the scene
    /// graph synchronises, when the GUI thread is held still.
    QPointer<QSGTextureProvider> m_provider;
    QMetaObject::Connection m_connection;
};

} // namespace Kerfur

#endif // KERFUR_TEXTUREWATCH_H
