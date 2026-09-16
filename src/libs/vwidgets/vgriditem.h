//---------------------------------------------------------------------------------------------------------------------
//  @file   vgriditem.h
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

#ifndef VGRIDITEM_H
#define VGRIDITEM_H

#include <QGraphicsItem>
#include <QPainterPath>
#include <QColor>

#include "../vmisc/def.h"

/// @brief A passive, non-interactive overlay that draws a configurable rectangular
/// grid (e.g. a knitting stitch/row grid) clipped to a piece's boundary.
class VGridItem : public QGraphicsItem
{
public:
    explicit             VGridItem(QGraphicsItem *parent = nullptr);
    virtual             ~VGridItem() Q_DECL_EQ_DEFAULT;

    void                 updateGeometry(const QPainterPath &clipPath, const QPointF &origin, qreal spacingX,
                                        qreal spacingY, qreal rotationDegrees, const QColor &color,
                                        Qt::PenStyle lineType, qreal lineWeight);

    virtual QRectF       boundingRect() const override;
    virtual void         paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    virtual int          type() const override {return Type;}
    enum                 {Type = UserType + static_cast<int>(Vis::GridItem)};

private:
    Q_DISABLE_COPY(VGridItem)

    QPainterPath         m_clipPath;
    QPointF              m_origin;
    qreal                m_spacingX;
    qreal                m_spacingY;
    qreal                m_rotation;
    QColor               m_color;
    Qt::PenStyle         m_lineType;
    qreal                m_lineWeight;
};

#endif // VGRIDITEM_H
