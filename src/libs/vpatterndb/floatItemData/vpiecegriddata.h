//---------------------------------------------------------------------------------------------------------------------
//  @file   vpiecegriddata.h
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

#ifndef VPIECEGRIDDATA_H
#define VPIECEGRIDDATA_H

#include <QString>

#include "vabstractfloatitemdata.h"

class VPieceGridDataPrivate;

//---------------------------------------------------------------------------------------------------------------------
/// @brief The VPieceGridData class holds information about a piece's configurable knitting grid:
/// spacing, rotation, line style and visibility.
//---------------------------------------------------------------------------------------------------------------------
class VPieceGridData : public VAbstractFloatItemData
{
public:
                    VPieceGridData();
                    VPieceGridData(const VPieceGridData &data);

    virtual        ~VPieceGridData();

    VPieceGridData &operator=(const VPieceGridData &data);
#ifdef Q_COMPILER_RVALUE_REFS
    VPieceGridData &operator=(VPieceGridData &&data) noexcept;
#endif

    void            Swap(VPieceGridData &data) noexcept;

    QString         getSpacingX() const;
    void            setSpacingX(const QString &spacing);

    QString         getSpacingY() const;
    void            setSpacingY(const QString &spacing);

    QString         getRotation() const;
    void            setRotation(const QString &rotation);

    QString         getLineColor() const;
    void            setLineColor(const QString &color);

    Qt::PenStyle    getLineType() const;
    void            setLineType(Qt::PenStyle type);

    QString         getLineWeight() const;
    void            setLineWeight(const QString &weight);

private:
    QSharedDataPointer<VPieceGridDataPrivate> d;
};

#endif // VPIECEGRIDDATA_H
