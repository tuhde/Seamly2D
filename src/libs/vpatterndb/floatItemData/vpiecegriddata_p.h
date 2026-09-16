//---------------------------------------------------------------------------------------------------------------------
//  @file   vpiecegriddata_p.h
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

#ifndef VPIECEGRIDDATA_P_H
#define VPIECEGRIDDATA_P_H

#include <QString>
#include <QSharedData>

#include "../vmisc/diagnostic.h"
#include "../ifc/ifcdef.h"

QT_WARNING_PUSH
QT_WARNING_DISABLE_GCC("-Weffc++")
QT_WARNING_DISABLE_GCC("-Wnon-virtual-dtor")

class VPieceGridDataPrivate : public QSharedData
{
public:
    VPieceGridDataPrivate()
        : m_spacingX()
        , m_spacingY()
        , m_rotation()
        , m_lineColor(QStringLiteral("black"))
        , m_lineType(Qt::DashLine)
        , m_lineWeight(QStringLiteral("0.2"))
    {}

    VPieceGridDataPrivate(const VPieceGridDataPrivate &data)
        : QSharedData(data)
        , m_spacingX(data.m_spacingX)
        , m_spacingY(data.m_spacingY)
        , m_rotation(data.m_rotation)
        , m_lineColor(data.m_lineColor)
        , m_lineType(data.m_lineType)
        , m_lineWeight(data.m_lineWeight)
    {}

    ~VPieceGridDataPrivate() Q_DECL_EQ_DEFAULT;

    QString      m_spacingX;   /// @brief m_spacingX formula for the horizontal grid line spacing */
    QString      m_spacingY;   /// @brief m_spacingY formula for the vertical grid line spacing */
    QString      m_rotation;   /// @brief m_rotation formula for the rotation of the grid in [degrees] */
    QString      m_lineColor;  /// @brief m_lineColor color of the grid lines */
    Qt::PenStyle m_lineType;   /// @brief m_lineType style of the grid lines */
    QString      m_lineWeight; /// @brief m_lineWeight formula for the width of the grid lines */

private:
    VPieceGridDataPrivate &operator=(const VPieceGridDataPrivate &) Q_DECL_EQ_DELETE;
};

QT_WARNING_POP

#endif // VPIECEGRIDDATA_P_H
