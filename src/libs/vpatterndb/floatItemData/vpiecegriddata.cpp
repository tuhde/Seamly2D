//---------------------------------------------------------------------------------------------------------------------
//  @file   vpiecegriddata.cpp
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

#include "vpiecegriddata.h"
#include "vpiecegriddata_p.h"

#ifdef Q_COMPILER_RVALUE_REFS
VPieceGridData &VPieceGridData::operator=(VPieceGridData &&data) noexcept { Swap(data); return *this; }
#endif

void VPieceGridData::Swap(VPieceGridData &data) noexcept
{ VAbstractFloatItemData::Swap(data); std::swap(d, data.d); }

//---------------------------------------------------------------------------------------------------------------------
VPieceGridData::VPieceGridData()
    : VAbstractFloatItemData()
    , d(new VPieceGridDataPrivate())
{}

//---------------------------------------------------------------------------------------------------------------------
VPieceGridData::VPieceGridData(const VPieceGridData &data)
    : VAbstractFloatItemData(data)
    , d(data.d)
{}

//---------------------------------------------------------------------------------------------------------------------
VPieceGridData &VPieceGridData::operator=(const VPieceGridData &data)
{
    if (&data == this)
    {
        return *this;
    }
    VAbstractFloatItemData::operator=(data);
    d = data.d;
    return *this;
}

//---------------------------------------------------------------------------------------------------------------------
VPieceGridData::~VPieceGridData()
{}

//---------------------------------------------------------------------------------------------------------------------
QString VPieceGridData::getSpacingX() const
{
    return d->m_spacingX;
}

//---------------------------------------------------------------------------------------------------------------------
void VPieceGridData::setSpacingX(const QString &spacing)
{
    d->m_spacingX = spacing;
}

//---------------------------------------------------------------------------------------------------------------------
QString VPieceGridData::getSpacingY() const
{
    return d->m_spacingY;
}

//---------------------------------------------------------------------------------------------------------------------
void VPieceGridData::setSpacingY(const QString &spacing)
{
    d->m_spacingY = spacing;
}

//---------------------------------------------------------------------------------------------------------------------
QString VPieceGridData::getRotation() const
{
    return d->m_rotation;
}

//---------------------------------------------------------------------------------------------------------------------
void VPieceGridData::setRotation(const QString &rotation)
{
    d->m_rotation = rotation;
}

//---------------------------------------------------------------------------------------------------------------------
QString VPieceGridData::getLineColor() const
{
    return d->m_lineColor;
}

//---------------------------------------------------------------------------------------------------------------------
void VPieceGridData::setLineColor(const QString &color)
{
    d->m_lineColor = color;
}

//---------------------------------------------------------------------------------------------------------------------
Qt::PenStyle VPieceGridData::getLineType() const
{
    return d->m_lineType;
}

//---------------------------------------------------------------------------------------------------------------------
void VPieceGridData::setLineType(Qt::PenStyle type)
{
    d->m_lineType = type;
}

//---------------------------------------------------------------------------------------------------------------------
QString VPieceGridData::getLineWeight() const
{
    return d->m_lineWeight;
}

//---------------------------------------------------------------------------------------------------------------------
void VPieceGridData::setLineWeight(const QString &weight)
{
    d->m_lineWeight = weight;
}
