// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "texturewatch.h"

namespace Kerfur {

TextureWatch::TextureWatch(QQuickItem *parent)
    : QQuickItem{parent}
{
    // Without contents the scene graph never calls updatePaintNode, which is
    // the one place the texture provider may be asked for.
    setFlag(ItemHasContents, true);
}

void TextureWatch::setSource(QQuickItem *source)
{
    if (m_source == source) {
        return;
    }
    QObject::disconnect(m_sourceDestroyed);
    m_source = source;
    if (source != nullptr) {
        m_sourceDestroyed = connect(source, &QObject::destroyed, this, [this]() { update(); });
    }
    update();
    emit sourceChanged();
}

QSGNode *TextureWatch::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    delete node;

    QSGTextureProvider *provider{nullptr};
    if (m_source != nullptr && m_source->window() == window() && m_source->isTextureProvider()) {
        provider = m_source->textureProvider();
    }
    if (provider != m_provider) {
        QObject::disconnect(m_connection);
        m_provider = provider;
        if (provider != nullptr) {
            // The provider lives on the render thread, and the redraw it
            // reports is decided there; the signal is for the GUI thread.
            m_connection = connect(provider, &QSGTextureProvider::textureChanged, this,
                                   &TextureWatch::textureChanged, Qt::QueuedConnection);
        }
    }
    return nullptr;
}

void TextureWatch::itemChange(ItemChange change, const ItemChangeData &value)
{
    // A new window, or a visible watch, is synchronised again and hooks onto
    // the provider of that window.
    if (change == ItemSceneChange || change == ItemVisibleHasChanged) {
        update();
    }
    QQuickItem::itemChange(change, value);
}

} // namespace Kerfur
