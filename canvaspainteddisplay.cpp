//
// Copyright (C) 2018 James Turner  <james@flightgear.org>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "canvaspainteddisplay.h"

#include <QDebug>

#include "canvasconnection.h"
#include "fgcanvasgroup.h"
#include "fgcanvaspaintcontext.h"
#include "localprop.h"

CanvasPaintedDisplay::CanvasPaintedDisplay(QQuickItem* parent) :
    QQuickPaintedItem(parent)
{
    setTransformOrigin(QQuickItem::TopLeft);
    setAntialiasing(true);
}

CanvasPaintedDisplay::~CanvasPaintedDisplay()
{
    qDebug() << "did destory canvas painted";
}

void CanvasPaintedDisplay::paint(QPainter *painter)
{
    if (!m_rootElement)
        return;

    const double xScaleFactor = width() / m_sourceSize.width();
    const double yScaleFactor =  height() / m_sourceSize.height();
    const double f = std::min(xScaleFactor, yScaleFactor);
    painter->scale(f, f);

    FGCanvasPaintContext context(painter);
    m_rootElement->paint(&context);

}

void CanvasPaintedDisplay::geometryChanged(const QRectF &newGeometry, const QRectF &)
{
    Q_UNUSED(newGeometry);
    update();
}

void CanvasPaintedDisplay::setCanvas(CanvasConnection *canvas)
{
    if (m_connection == canvas)
        return;

    if (m_connection) {
        disconnect(m_connection, nullptr, this, nullptr);
        m_rootElement.reset();
    }

    m_connection = canvas;
    emit canvasChanged(m_connection);

    if (m_connection) {
        connect(m_connection, &QObject::destroyed,
                this, &CanvasPaintedDisplay::onConnectionDestroyed);
        connect(m_connection, &CanvasConnection::statusChanged,
                this, &CanvasPaintedDisplay::onConnectionStatusChanged);
        connect(m_connection, &CanvasConnection::updated,
                this, &CanvasPaintedDisplay::onConnectionUpdated);

        onConnectionStatusChanged();
    }
}

void CanvasPaintedDisplay::onConnectionDestroyed()
{
    m_connection = nullptr;
    emit canvasChanged(m_connection);

    m_rootElement.reset();
}

void CanvasPaintedDisplay::onConnectionStatusChanged()
{
    if ((m_connection->status() == CanvasConnection::Connected) ||
            (m_connection->status() == CanvasConnection::Snapshot))
    {
        buildElements();
    }
}

void CanvasPaintedDisplay::buildElements()
{
    m_rootElement.reset(new FGCanvasGroup(nullptr, m_connection->propertyRoot()));
    // this is important to elements can discover their connection
    // by walking their parent chain
    m_rootElement->setParent(m_connection);

    connect(m_rootElement.get(), &FGCanvasGroup::canvasSizeChanged,
            this, &CanvasPaintedDisplay::onCanvasSizeChanged);

    onCanvasSizeChanged();

    m_connection->propertyRoot()->recursiveNotifyRestored();
    m_rootElement->polish();
    update();
}

void CanvasPaintedDisplay::onConnectionUpdated()
{
    if (m_rootElement) {
        m_rootElement->polish();
        update();
    }
}

void CanvasPaintedDisplay::onCanvasSizeChanged()
{
    m_sourceSize = QSizeF(m_connection->propertyRoot()->value("size", 256).toDouble(),
                          m_connection->propertyRoot()->value("size[1]", 256).toDouble());
    setImplicitSize(m_sourceSize.width(), m_sourceSize.height());
    update();
}

