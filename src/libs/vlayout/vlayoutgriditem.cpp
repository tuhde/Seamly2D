//---------------------------------------------------------------------------------------------------------------------
//  @file   vlayoutgriditem.cpp
//  @author Seamly2D Team
//
//  @copyright
//  Copyright (C) 2017 - 2026 Seamly, LLC
//  https://github.com/fashionfreedom/seamly2d
//
//  @brief
//  Seamly2D is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  Seamly2D is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with Seamly2D. If not, see <http://www.gnu.org/licenses/>.
//---------------------------------------------------------------------------------------------------------------------

#include "vlayoutgriditem.h"

#include <QLineF>
#include <QPainter>
#include <QtMath>

//---------------------------------------------------------------------------------------------------------------------
VLayoutGridItem::VLayoutGridItem(QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_clipPath()
    , m_origin()
    , m_spacingX(0)
    , m_spacingY(0)
    , m_rotation(0)
    , m_color(Qt::black)
    , m_lineType(Qt::DashLine)
    , m_lineWeight(1)
{}

//---------------------------------------------------------------------------------------------------------------------
void VLayoutGridItem::updateGeometry(const QPainterPath &clipPath, const QPointF &origin, qreal spacingX,
                                     qreal spacingY, qreal rotationDegrees, const QColor &color,
                                     Qt::PenStyle lineType, qreal lineWeight)
{
    prepareGeometryChange();
    m_clipPath  = clipPath;
    m_origin    = origin;
    m_spacingX  = spacingX;
    m_spacingY  = spacingY;
    m_rotation  = rotationDegrees;
    m_color     = color;
    m_lineType  = lineType;
    m_lineWeight = lineWeight;
}

//---------------------------------------------------------------------------------------------------------------------
QRectF VLayoutGridItem::boundingRect() const
{
    return m_clipPath.boundingRect();
}

//---------------------------------------------------------------------------------------------------------------------
void VLayoutGridItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    if (m_clipPath.isEmpty() || m_spacingX <= 0 || m_spacingY <= 0)
    {
        return;
    }

    painter->save();
    painter->setClipPath(m_clipPath);
    painter->setPen(QPen(m_color, m_lineWeight, m_lineType));

    // Anchor the grid on the piece's base point (origin): translate/rotate around it so a grid
    // line always runs through the origin itself, then draw far enough in every direction to
    // cover the piece regardless of rotation.
    painter->translate(m_origin);
    painter->rotate(m_rotation);

    const QRectF rect = m_clipPath.boundingRect();
    qreal radius = 0;
    const QPointF corners[4] = {rect.topLeft(), rect.topRight(), rect.bottomLeft(), rect.bottomRight()};
    for (const QPointF &corner : corners)
    {
        radius = qMax(radius, QLineF(m_origin, corner).length());
    }

    const int countX = qCeil(radius / m_spacingX);
    for (int i = -countX; i <= countX; ++i)
    {
        const qreal x = i * m_spacingX;
        painter->drawLine(QPointF(x, -radius), QPointF(x, radius));
    }

    const int countY = qCeil(radius / m_spacingY);
    for (int i = -countY; i <= countY; ++i)
    {
        const qreal y = i * m_spacingY;
        painter->drawLine(QPointF(-radius, y), QPointF(radius, y));
    }

    painter->restore();
}
