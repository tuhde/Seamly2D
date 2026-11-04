/***************************************************************************
 **  @file   pattern_editor.cpp
 **
 **  @brief  Reads and changes the pattern open in the main window for the MCP tools.
 **  @copyright
 **  This source code is part of the Seamly2D project, a pattern making
 **  program to create and model patterns of clothing.
 **  Copyright (C) 2017-2026 Seamly2D project
 **  <https://github.com/fashionfreedom/seamly2d> All Rights Reserved.
 **
 **  Seamly2D is free software: you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation, either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Seamly2D is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Seamly2D.  If not, see <http://www.gnu.org/licenses/>.
 **
 **************************************************************************/

#include "pattern_editor.h"

#include <QBuffer>
#include <cmath>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QDomElement>
#include <QDomNamedNodeMap>
#include <QHash>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QList>
#include <QSet>
#include <QSharedPointer>
#include <QVector>
#include <QUndoCommand>
#include <QUndoStack>
#include <QtMath>

#include "../core/application_2d.h"
#include "../mainwindow.h"
#include "../xml/vpattern.h"
#include "../../libs/ifc/exception/vexception.h"
#include "../../libs/ifc/ifcdef.h"
#include "../../libs/ifc/xml/vabstractpattern.h"
#include "../../libs/qmuparser/qmuparsererror.h"
#include "../../libs/vgeometry/vabstractcurve.h"
#include "../../libs/vgeometry/varc.h"
#include "../../libs/vgeometry/vcubicbezier.h"
#include "../../libs/vgeometry/vpointf.h"
#include "../../libs/vpatterndb/calculator.h"
#include "../../libs/vpatterndb/vcontainer.h"
#include "../../libs/vpatterndb/variables/vinternalvariable.h"
#include "../../libs/vpatterndb/variables/custom_variable.h"
#include "../../libs/vpatterndb/variables/vlinelength.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/intersect_arc_line_tool.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/intersect_circles_tool.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/intersect_circletangent_tool.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/vtoolpointfromarcandtangent.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/vtoolpointofintersectionarcs.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/vtoolpointofintersectioncurves.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/point_intersectxy_tool.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/toolcut/vtoolcutarc.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/toolcut/vtoolcutspline.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/toolcut/vtoolcutsplinepath.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolcurveintersectaxis.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolheight.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoollineintersectaxis.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolshoulderpoint.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/vtooltriangle.h"
#include "../../libs/vtools/tools/drawTools/vtoolline.h"
#include "../../libs/vtools/tools/drawTools/toolcurve/vtoolarc.h"
#include "../../libs/vtools/tools/drawTools/toolcurve/vtoolcubicbezier.h"
#include "../../libs/vtools/tools/drawTools/toolcurve/vtoolsplinepath.h"
#include "../../libs/vtools/tools/drawTools/toolcurve/vtoolspline.h"
#include "../../libs/vtools/tools/pattern_piece_tool.h"
#include "../../libs/vtools/tools/vabstracttool.h"
#include "../../libs/vpatterndb/floatItemData/vgrainlinedata.h"
#include "../../libs/vpatterndb/floatItemData/vpiecelabeldata.h"
#include "../../libs/vpatterndb/vpiece.h"
#include "../../libs/vpatterndb/vtranslatemeasurements.h"
#include "../../libs/vpatterndb/vpiecenode.h"
#include "../../libs/vpatterndb/vpiecepath.h"
#include "../../libs/vtools/undocommands/deletepiece.h"
#include "../../libs/vtools/undocommands/savepieceoptions.h"
#include "../../libs/vtools/undocommands/deltool.h"
#include "../../libs/vtools/undocommands/savetooloptions.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/vtoollineintersect.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolalongline.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolbisector.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolnormal.h"
#include "../../libs/vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolendline.h"
#include "../../libs/vwidgets/vmaingraphicsscene.h"
#include "../../libs/vwidgets/vmaingraphicsview.h"

namespace
{
/// Everything a tool needs from the open pattern.
struct Context
{
    VAbstractPattern   *doc   = nullptr;
    VContainer         *data  = nullptr;
    VMainGraphicsScene *scene = nullptr;
    QString             error;

    bool valid() const { return error.isEmpty(); }
};

Context currentContext()
{
    Context ctx;
    ctx.doc   = qApp->getCurrentDocument();
    ctx.data  = qApp->getCurrentData();
    ctx.scene = qobject_cast<VMainGraphicsScene *>(qApp->getCurrentScene());
    if (ctx.doc == nullptr || ctx.data == nullptr || ctx.scene == nullptr)
    {
        ctx.error = QStringLiteral("No pattern is open in Seamly2D.");
    }
    else if (ctx.doc->getActiveDraftBlockName().isEmpty())
    {
        ctx.error = QStringLiteral("The pattern has no draft block yet. Create one first with the tool add_draft_block (a base "
                                   "point is created with it); then draw the other points.");
    }
    return ctx;
}

QJsonObject attributesOf(const QDomElement &element)
{
    QJsonObject result;
    const QDomNamedNodeMap attributes = element.attributes();
    for (int i = 0; i < attributes.count(); ++i)
    {
        const QDomAttr attribute = attributes.item(i).toAttr();
        result.insert(attribute.name(), attribute.value());
    }
    return result;
}

double round3(double value)
{
    return qRound64(value * 1000.0) / 1000.0;
}

QJsonObject pointJson(quint32 id, const VPointF &point)
{
    return QJsonObject{{QStringLiteral("id"), static_cast<qint64>(id)},
                       {QStringLiteral("name"), point.name()},
                       {QStringLiteral("x"), round3(qApp->fromPixel(point.x()))},
                       {QStringLiteral("y"), round3(qApp->fromPixel(point.y()))}};
}

QString stringArg(const QJsonObject &args, const char *key)
{
    return args.value(QLatin1String(key)).toString().trimmed();
}

/// Reads an id argument and checks that it refers to an existing point of the active draft block.
bool pointArg(const Context &ctx, const QJsonObject &args, const char *key, quint32 &id, QString &error)
{
    const QJsonValue value = args.value(QLatin1String(key));
    if (!value.isDouble() || value.toDouble() < 1 || value.toDouble() != qFloor(value.toDouble()))
    {
        error = QStringLiteral("'%1' is required and must be the id of an existing point (see list_objects).")
                    .arg(QLatin1String(key));
        return false;
    }
    id = static_cast<quint32>(value.toDouble());
    try
    {
        const QSharedPointer<VGObject> object = ctx.data->GetGObject(id);
        if (object.isNull() || object->getType() != GOType::Point)
        {
            error = QStringLiteral("'%1' = %2 is not a point.").arg(QLatin1String(key)).arg(id);
            return false;
        }
    }
    catch (const VException &)
    {
        error = QStringLiteral("'%1' = %2 does not exist in the active draft block (see list_objects).")
                    .arg(QLatin1String(key)).arg(id);
        return false;
    }
    return true;
}

/// Evaluates a formula without ever opening the "fix formula" dialog. Returns false and sets `error` when invalid.
bool checkFormula(const QHash<QString, QSharedPointer<VInternalVariable>> &variables, const QString &name,
                  const QString &formula, double &value, QString &error)
{
    if (formula.isEmpty())
    {
        error = QStringLiteral("'%1' is required (a number or a formula such as \"10\" or \"waist_circ/4\").").arg(name);
        return false;
    }
    try
    {
        Calculator calculator;
        value = calculator.EvalFormula(&variables, formula);
    }
    catch (const qmu::QmuParserError &e)
    {
        error = QStringLiteral("Invalid formula for '%1' (\"%2\"): %3").arg(name, formula, e.GetMsg());
        return false;
    }
    if (qIsInf(value) || qIsNaN(value))
    {
        error = QStringLiteral("Formula for '%1' (\"%2\") has no finite value.").arg(name, formula);
        return false;
    }
    return true;
}

/// True if the element of the calculation section draws a visible line between the points `a` and `b`: either a
/// `line` element, or a point that is drawn together with a line from its base point (the base is stored under a
/// different attribute per tool type).
bool drawsLineBetween(const QDomElement &element, quint32 a, quint32 b)
{
    quint32 from = NULL_ID;
    quint32 to = NULL_ID;
    if (element.tagName() == QLatin1String("line"))
    {
        from = element.attribute(AttrFirstPoint).toUInt();
        to   = element.attribute(AttrSecondPoint).toUInt();
    }
    else if (element.tagName() == QLatin1String("point"))
    {
        const QString type = element.attribute(AttrType);
        to = element.attribute(QStringLiteral("id")).toUInt();
        if (type == VToolEndLine::ToolType)
        {
            from = element.attribute(AttrBasePoint).toUInt();
        }
        else if (type == VToolAlongLine::ToolType || type == VToolNormal::ToolType)
        {
            from = element.attribute(AttrFirstPoint).toUInt();
        }
        else if (type == VToolBisector::ToolType)
        {
            from = element.attribute(AttrSecondPoint).toUInt();   // the vertex
        }
    }
    return from != NULL_ID && to != NULL_ID && ((from == a && to == b) || (from == b && to == a));
}

QJsonObject stringProperty(const QString &description)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("description"), description}};
}

QJsonObject idProperty(const QString &description)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                       {QStringLiteral("minimum"), 1},
                       {QStringLiteral("description"), description}};
}
} // namespace

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::listObjects(const QJsonObject &arguments)
{
    const Context ctx = currentContext();
    if (!ctx.valid())
    {
        return McpToolResult::error(ctx.error);
    }

    QDomElement calculation;
    if (!ctx.doc->getActiveNodeElement(VAbstractPattern::TagCalculation, calculation))
    {
        return McpToolResult::error(QStringLiteral("The active draft block has no calculation section."));
    }

    // Optional filters.
    const QString elementFilter = stringArg(arguments, "element");
    const QString typeFilter    = stringArg(arguments, "type");
    const QString nameFilter    = stringArg(arguments, "name_contains");
    QSet<quint32> idFilter;
    const QJsonValue idsValue = arguments.value(QStringLiteral("ids"));
    if (idsValue.isArray())
    {
        const QJsonArray ids = idsValue.toArray();
        for (const QJsonValue &id : ids)
        {
            idFilter.insert(static_cast<quint32>(id.toDouble()));
        }
    }
    else if (!idsValue.isUndefined() && !idsValue.isNull())
    {
        return McpToolResult::error(QStringLiteral("'ids' must be an array of object ids."));
    }
    // Presentation details (label offsets, colors, ...) do not matter for constructing; leave them out.
    static const QStringList noise{QStringLiteral("mx"), QStringLiteral("my"), QStringLiteral("showPointName"),
                                   QStringLiteral("showLabel"), QStringLiteral("lineColor"),
                                   QStringLiteral("lineWeight"), QStringLiteral("penStyle"), QStringLiteral("color")};

    int total = 0;
    QJsonArray objects;
    for (QDomNode node = calculation.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        const QDomElement element = node.toElement();
        if (element.isNull())
        {
            continue;
        }
        ++total;
        if ((!elementFilter.isEmpty() && element.tagName() != elementFilter)
            || (!typeFilter.isEmpty() && element.attribute(AttrType) != typeFilter)
            || (!nameFilter.isEmpty()
                && !element.attribute(QStringLiteral("name")).contains(nameFilter, Qt::CaseInsensitive))
            || (!idFilter.isEmpty() && !idFilter.contains(element.attribute(QStringLiteral("id")).toUInt())))
        {
            continue;
        }
        QJsonObject definition = attributesOf(element);
        for (const QString &key : noise)
        {
            definition.remove(key);
        }
        QJsonObject item{{QStringLiteral("element"), element.tagName()},
                         {QStringLiteral("definition"), definition}};
        // Child elements, e.g. the points of a path curve (pSpline, angle1, angle2, length1, length2, ...).
        QJsonArray children;
        for (QDomNode child = element.firstChild(); !child.isNull(); child = child.nextSibling())
        {
            if (child.isElement() && child.toElement().tagName() != QLatin1String("node"))
            {
                children.append(attributesOf(child.toElement()));
            }
        }
        if (!children.isEmpty())
        {
            item.insert(QStringLiteral("children"), children);
        }

        bool ok = false;
        const quint32 id = element.attribute(QStringLiteral("id")).toUInt(&ok);
        if (ok)
        {
            item.insert(QStringLiteral("id"), static_cast<qint64>(id));
            try
            {
                const QSharedPointer<VGObject> object = ctx.data->GetGObject(id);
                if (!object.isNull() && object->getType() == GOType::Point)
                {
                    const QSharedPointer<VPointF> point = ctx.data->GeometricObject<VPointF>(id);
                    const QJsonObject position = pointJson(id, *point);
                    item.insert(QStringLiteral("name"), position.value(QStringLiteral("name")));
                    item.insert(QStringLiteral("x"), position.value(QStringLiteral("x")));
                    item.insert(QStringLiteral("y"), position.value(QStringLiteral("y")));
                }
            }
            catch (const VException &)
            {
                // Not a geometric object known to the container (e.g. a line); the definition is enough.
            }
        }
        objects.append(item);
    }

    return McpToolResult::ok(QStringLiteral("%1 of %2 object(s) in draft block \"%3\" (unit: %4; y grows "
                                            "downwards). Use the ids as references.")
                                 .arg(objects.size()).arg(total)
                                 .arg(ctx.doc->getActiveDraftBlockName(), UnitsToStr(qApp->patternUnit())),
                             QJsonObject{{QStringLiteral("draftBlock"), ctx.doc->getActiveDraftBlockName()},
                                         {QStringLiteral("total"), total},
                                         {QStringLiteral("unit"), UnitsToStr(qApp->patternUnit())},
                                         {QStringLiteral("objects"), objects}});
}

namespace
{
/// Point types that are created by addAdvancedPoint().
const QStringList kAdvancedPointTypes{QStringLiteral("height"), QStringLiteral("lineIntersectAxis"),
                                      QStringLiteral("curveIntersectAxis"), QStringLiteral("cutCurve"),
                                      QStringLiteral("shoulder"), QStringLiteral("triangle"),
                                      QStringLiteral("intersectXY"), QStringLiteral("intersectCircles"),
                                      QStringLiteral("intersectCircleLine"), QStringLiteral("tangentToCircle"),
                                      QStringLiteral("intersectArcs"), QStringLiteral("tangentToArc"),
                                      QStringLiteral("intersectCurves")};

QString formulaArg(const QJsonObject &arguments, const char *key)
{
    const QJsonValue value = arguments.value(QLatin1String(key));
    return value.isDouble() ? QString::number(value.toDouble()) : value.toString().trimmed();
}

QPointF positionOf(const Context &ctx, quint32 id)
{
    return static_cast<QPointF>(*ctx.data->GeometricObject<VPointF>(id));
}

QSharedPointer<VArc> arcOf(const Context &ctx, quint32 id)
{
    try
    {
        const QSharedPointer<VGObject> object = ctx.data->GetGObject(id);
        if (!object.isNull() && object->getType() == GOType::Arc)
        {
            return ctx.data->GeometricObject<VArc>(id);
        }
    }
    catch (const VException &)
    {
    }
    return QSharedPointer<VArc>();
}

QSharedPointer<VAbstractCurve> curveOf(const Context &ctx, quint32 id)
{
    try
    {
        return ctx.data->GeometricObject<VAbstractCurve>(id);
    }
    catch (const VException &)
    {
    }
    return QSharedPointer<VAbstractCurve>();
}

/// Reads an object id argument (not checked for its kind).
bool idArg(const QJsonObject &arguments, const char *key, quint32 &id, QString &error)
{
    const QJsonValue value = arguments.value(QLatin1String(key));
    if (!value.isDouble() || value.toDouble() < 1 || value.toDouble() != qFloor(value.toDouble()))
    {
        error = QStringLiteral("'%1' is required: an object id (see list_objects).").arg(QLatin1String(key));
        return false;
    }
    id = static_cast<quint32>(value.toDouble());
    return true;
}

/// "first" / "second" (default first) for the two intersections of circles; sets `error` for anything else.
CrossCirclesPoint crossingArg(const QJsonObject &arguments, QString &error)
{
    const QString crossing = stringArg(arguments, "crossing").isEmpty() ? QStringLiteral("first")
                                                                          : stringArg(arguments, "crossing").toLower();
    if (crossing != QLatin1String("first") && crossing != QLatin1String("second"))
    {
        error = QStringLiteral("'crossing' must be \"first\" or \"second\" (which of the two intersections).");
    }
    return crossing == QLatin1String("second") ? CrossCirclesPoint::SecondPoint : CrossCirclesPoint::FirstPoint;
}
} // namespace

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::addAdvancedPoint(const QJsonObject &arguments, const QString &type, const QString &name)
{
    const Context ctx = currentContext();
    if (!ctx.valid())
    {
        return McpToolResult::error(ctx.error);
    }
    QString error;
    const QHash<QString, QSharedPointer<VInternalVariable>> variables = *ctx.data->DataVariables();
    double value = 0;
    quint32 base = NULL_ID;
    quint32 p1 = NULL_ID;
    quint32 p2 = NULL_ID;
    quint32 p3 = NULL_ID;
    quint32 p4 = NULL_ID;
    QString angle = formulaArg(arguments, "angle");
    QString length = formulaArg(arguments, "length");
    double angleValue = 0;
    double lengthValue = 0;
    QString direction = QStringLiteral("forward");
    QString radius1 = formulaArg(arguments, "radius1").isEmpty() ? formulaArg(arguments, "radius") : formulaArg(arguments, "radius1");
    QString radius2 = formulaArg(arguments, "radius2");
    CrossCirclesPoint crossing = CrossCirclesPoint::FirstPoint;
    VCrossCurvesPoint vCross = VCrossCurvesPoint::HighestPoint;
    HCrossCurvesPoint hCross = HCrossCurvesPoint::LeftmostPoint;

    // --- validate (nothing is changed yet) ---------------------------------------------------------------------------
    if (type == QLatin1String("height"))
    {
        if (!pointArg(ctx, arguments, "base_point_id", base, error)
            || !pointArg(ctx, arguments, "line_point1_id", p1, error)
            || !pointArg(ctx, arguments, "line_point2_id", p2, error))
        {
            return McpToolResult::error(error);
        }
        if (p1 == p2)
        {
            return McpToolResult::error(QStringLiteral("'line_point1_id' and 'line_point2_id' must differ."));
        }
    }
    else if (type == QLatin1String("lineIntersectAxis"))
    {
        if (!pointArg(ctx, arguments, "base_point_id", base, error)
            || !pointArg(ctx, arguments, "first_point_id", p1, error)
            || !pointArg(ctx, arguments, "second_point_id", p2, error)
            || !checkFormula(variables, QStringLiteral("angle"), angle, angleValue, error))
        {
            return McpToolResult::error(error);
        }
        if (p1 == p2)
        {
            return McpToolResult::error(QStringLiteral("'first_point_id' and 'second_point_id' must differ."));
        }
        QLineF axis(positionOf(ctx, base), positionOf(ctx, base) + QPointF(100, 0));
        axis.setAngle(angleValue);
        if (VToolLineIntersectAxis::FindPoint(axis, QLineF(positionOf(ctx, p1), positionOf(ctx, p2))).isNull())
        {
            return McpToolResult::error(QStringLiteral("The axis through the base point at this angle does not cross "
                                                       "the line (they are parallel)."));
        }
    }
    else if (type == QLatin1String("curveIntersectAxis"))
    {
        quint32 curve = NULL_ID;
        if (!pointArg(ctx, arguments, "base_point_id", base, error)
            || !checkFormula(variables, QStringLiteral("angle"), angle, angleValue, error))
        {
            return McpToolResult::error(error);
        }
        const QJsonValue curveValue = arguments.value(QStringLiteral("curve_id"));
        if (!curveValue.isDouble() || curveValue.toDouble() < 1)
        {
            return McpToolResult::error(QStringLiteral("'curve_id' is required: the id of a curve or arc (see "
                                                       "list_objects)."));
        }
        curve = static_cast<quint32>(curveValue.toDouble());
        QSharedPointer<VAbstractCurve> curveObject;
        try
        {
            curveObject = ctx.data->GeometricObject<VAbstractCurve>(curve);
        }
        catch (const VException &)
        {
        }
        if (curveObject.isNull())
        {
            return McpToolResult::error(QStringLiteral("'curve_id' = %1 is not a curve or arc of the active draft "
                                                       "block.").arg(curve));
        }
        QPointF crossing;
        if (!VToolCurveIntersectAxis::FindPoint(positionOf(ctx, base), angleValue, curveObject, &crossing))
        {
            return McpToolResult::error(QStringLiteral("The axis through the base point at this angle does not cross "
                                                       "the curve."));
        }
        p1 = curve;
    }
    else if (type == QLatin1String("cutCurve"))
    {
        const QJsonValue curveValue = arguments.value(QStringLiteral("curve_id"));
        if (!curveValue.isDouble() || curveValue.toDouble() < 1)
        {
            return McpToolResult::error(QStringLiteral("'curve_id' is required: the id of a spline, path or arc (see "
                                                       "list_objects)."));
        }
        p1 = static_cast<quint32>(curveValue.toDouble());
        QSharedPointer<VGObject> object;
        try
        {
            object = ctx.data->GetGObject(p1);
        }
        catch (const VException &)
        {
        }
        const GOType kind = object.isNull() ? GOType::Unknown : object->getType();
        if (kind != GOType::Spline && kind != GOType::CubicBezier && kind != GOType::SplinePath
            && kind != GOType::CubicBezierPath && kind != GOType::Arc)
        {
            return McpToolResult::error(QStringLiteral("'curve_id' = %1 is not a spline, path or arc of the active "
                                                       "draft block.").arg(p1));
        }
        if (!checkFormula(variables, QStringLiteral("length"), length, lengthValue, error))
        {
            return McpToolResult::error(error);
        }
        const QString from = stringArg(arguments, "from").isEmpty() ? QStringLiteral("start")
                                                                      : stringArg(arguments, "from").toLower();
        if (from != QLatin1String("start") && from != QLatin1String("end"))
        {
            return McpToolResult::error(QStringLiteral("'from' must be \"start\" or \"end\"."));
        }
        direction = from == QLatin1String("start") ? QStringLiteral("forward") : QStringLiteral("backward");
        const double curveLength = qApp->fromPixel(ctx.data->GeometricObject<VAbstractCurve>(p1)->GetLength());
        if (lengthValue <= 0.01 || lengthValue >= curveLength - 0.01)
        {
            return McpToolResult::error(QStringLiteral("'length' must be between 0 and the length "
                                                       "of the curve (%1 %2); it is %3.")
                                            .arg(curveLength, 0, 'f', 3).arg(UnitsToStr(qApp->patternUnit()))
                                            .arg(lengthValue));
        }
    }
    else if (type == QLatin1String("shoulder"))
    {
        if (!pointArg(ctx, arguments, "line_point1_id", p1, error)
            || !pointArg(ctx, arguments, "line_point2_id", p2, error)
            || !pointArg(ctx, arguments, "shoulder_point_id", p3, error)
            || !checkFormula(variables, QStringLiteral("length"), length, lengthValue, error))
        {
            return McpToolResult::error(error);
        }
        if (p1 == p2)
        {
            return McpToolResult::error(QStringLiteral("'line_point1_id' and 'line_point2_id' must differ."));
        }
        // The program falls back to the second line point when no such point exists; report that instead.
        const QPointF found = VToolShoulderPoint::FindPoint(positionOf(ctx, p1), positionOf(ctx, p2),
                                                            positionOf(ctx, p3), qApp->toPixel(lengthValue));
        if (lengthValue <= 0 || found == positionOf(ctx, p2))
        {
            return McpToolResult::error(QStringLiteral("There is no point on the extension of the line beyond "
                                                       "'line_point2_id' at distance %1 from the shoulder point "
                                                       "(the distance is too short or the line points away).")
                                            .arg(length));
        }
    }
    else if (type == QLatin1String("triangle"))
    {
        if (!pointArg(ctx, arguments, "axis_point1_id", p1, error)
            || !pointArg(ctx, arguments, "axis_point2_id", p2, error)
            || !pointArg(ctx, arguments, "first_point_id", p3, error)
            || !pointArg(ctx, arguments, "second_point_id", p4, error))
        {
            return McpToolResult::error(error);
        }
        if (p1 == p2 || p3 == p4)
        {
            return McpToolResult::error(QStringLiteral("The axis needs two different points, and so does the "
                                                       "hypotenuse."));
        }
        // The program starts where the axis crosses the segment between the two points and walks along the axis until
        // it sees them at a right angle. Without a crossing inside that segment the result would be meaningless.
        const QLineF axisLine(positionOf(ctx, p1), positionOf(ctx, p2));
        const QLineF hypotenuse(positionOf(ctx, p3), positionOf(ctx, p4));
        QPointF crossing;
        if (axisLine.intersects(hypotenuse, &crossing) == QLineF::NoIntersection)
        {
            return McpToolResult::error(QStringLiteral("There is no such triangle: the axis and the line through the "
                                                       "two points are parallel."));
        }
        const QPointF d = hypotenuse.p2() - hypotenuse.p1();
        const double t = QPointF::dotProduct(crossing - hypotenuse.p1(), d) / qMax(1e-9, QPointF::dotProduct(d, d));
        if (t <= 0.0 || t >= 1.0)
        {
            return McpToolResult::error(QStringLiteral("The axis must cross the segment between 'first_point_id' and "
                                                       "'second_point_id' (it crosses the line outside of it)."));
        }
        const QPointF found = VToolTriangle::FindPoint(positionOf(ctx, p1), positionOf(ctx, p2), positionOf(ctx, p3),
                                                       positionOf(ctx, p4));
        const QLineF toFirst(found, positionOf(ctx, p3));
        const QLineF toSecond(found, positionOf(ctx, p4));
        const double cosine = (toFirst.dx() * toSecond.dx() + toFirst.dy() * toSecond.dy())
                              / qMax(1e-9, toFirst.length() * toSecond.length());
        if (found.isNull() || qAbs(cosine) > 0.01)
        {
            return McpToolResult::error(QStringLiteral("No point on the axis sees the two points at a right angle."));
        }
    }
    else if (type == QLatin1String("intersectCircles"))
    {
        if (!pointArg(ctx, arguments, "center1_point_id", p1, error)
            || !pointArg(ctx, arguments, "center2_point_id", p2, error)
            || !checkFormula(variables, QStringLiteral("radius1"), radius1, lengthValue, error)
            || !checkFormula(variables, QStringLiteral("radius2"), radius2, value, error))
        {
            return McpToolResult::error(error);
        }
        crossing = crossingArg(arguments, error);
        if (!error.isEmpty())
        {
            return McpToolResult::error(error);
        }
        if (p1 == p2 || lengthValue <= 0 || value <= 0)
        {
            return McpToolResult::error(QStringLiteral("The two circles need different centers and radii greater "
                                                       "than 0."));
        }
        if (IntersectCirclesTool::FindPoint(positionOf(ctx, p1), positionOf(ctx, p2), qApp->toPixel(lengthValue),
                                            qApp->toPixel(value), crossing).isNull())
        {
            return McpToolResult::error(QStringLiteral("The two circles do not intersect (they are too far apart, or "
                                                       "one lies inside the other)."));
        }
    }
    else if (type == QLatin1String("intersectCircleLine"))
    {
        if (!pointArg(ctx, arguments, "center_point_id", p1, error)
            || !pointArg(ctx, arguments, "line_point1_id", p2, error)
            || !pointArg(ctx, arguments, "line_point2_id", p3, error)
            || !checkFormula(variables, QStringLiteral("radius"), radius1, lengthValue, error))
        {
            return McpToolResult::error(error);
        }
        if (p2 == p3 || lengthValue <= 0)
        {
            return McpToolResult::error(QStringLiteral("The line needs two different points and the radius must be "
                                                       "greater than 0."));
        }
        if (IntersectArcLineTool::FindPoint(qApp->toPixel(lengthValue), positionOf(ctx, p1), positionOf(ctx, p2),
                                            positionOf(ctx, p3)).isNull())
        {
            return McpToolResult::error(QStringLiteral("The circle and the line do not meet."));
        }
    }
    else if (type == QLatin1String("tangentToCircle"))
    {
        if (!pointArg(ctx, arguments, "center_point_id", p1, error)
            || !pointArg(ctx, arguments, "tangent_point_id", p2, error)
            || !checkFormula(variables, QStringLiteral("radius"), radius1, lengthValue, error))
        {
            return McpToolResult::error(error);
        }
        crossing = crossingArg(arguments, error);
        if (!error.isEmpty())
        {
            return McpToolResult::error(error);
        }
        if (p1 == p2 || lengthValue <= 0)
        {
            return McpToolResult::error(QStringLiteral("The point must differ from the center and the radius must be "
                                                       "greater than 0."));
        }
        if (IntersectCircleTangentTool::FindPoint(positionOf(ctx, p2), positionOf(ctx, p1), qApp->toPixel(lengthValue),
                                                  crossing).isNull())
        {
            return McpToolResult::error(QStringLiteral("There is no tangent from this point to the circle: the point "
                                                       "lies inside the circle."));
        }
    }
    else if (type == QLatin1String("intersectArcs"))
    {
        if (!idArg(arguments, "first_arc_id", p1, error) || !idArg(arguments, "second_arc_id", p2, error))
        {
            return McpToolResult::error(error);
        }
        crossing = crossingArg(arguments, error);
        if (!error.isEmpty())
        {
            return McpToolResult::error(error);
        }
        const QSharedPointer<VArc> arc1 = arcOf(ctx, p1);
        const QSharedPointer<VArc> arc2 = arcOf(ctx, p2);
        if (arc1.isNull() || arc2.isNull() || p1 == p2)
        {
            return McpToolResult::error(QStringLiteral("'first_arc_id' and 'second_arc_id' must be two different arcs "
                                                       "of the active draft block."));
        }
        if (VToolPointOfIntersectionArcs::FindPoint(arc1.data(), arc2.data(), crossing).isNull())
        {
            return McpToolResult::error(QStringLiteral("The two arcs do not intersect (the circles do not meet, or "
                                                       "they meet outside of the arcs)."));
        }
    }
    else if (type == QLatin1String("tangentToArc"))
    {
        if (!idArg(arguments, "arc_id", p1, error) || !pointArg(ctx, arguments, "tangent_point_id", p2, error))
        {
            return McpToolResult::error(error);
        }
        crossing = crossingArg(arguments, error);
        if (!error.isEmpty())
        {
            return McpToolResult::error(error);
        }
        const QSharedPointer<VArc> arc = arcOf(ctx, p1);
        if (arc.isNull())
        {
            return McpToolResult::error(QStringLiteral("'arc_id' = %1 is not an arc of the active draft block.").arg(p1));
        }
        if (IntersectCircleTangentTool::FindPoint(positionOf(ctx, p2), static_cast<QPointF>(arc->GetCenter()),
                                                  arc->GetRadius(), crossing).isNull())
        {
            return McpToolResult::error(QStringLiteral("There is no tangent from this point to the arc: the point lies "
                                                       "inside its circle."));
        }
    }
    else if (type == QLatin1String("intersectCurves"))
    {
        if (!idArg(arguments, "first_curve_id", p1, error) || !idArg(arguments, "second_curve_id", p2, error))
        {
            return McpToolResult::error(error);
        }
        const QSharedPointer<VAbstractCurve> c1 = curveOf(ctx, p1);
        const QSharedPointer<VAbstractCurve> c2 = curveOf(ctx, p2);
        if (c1.isNull() || c2.isNull() || p1 == p2)
        {
            return McpToolResult::error(QStringLiteral("'first_curve_id' and 'second_curve_id' must be two different "
                                                       "curves or arcs of the active draft block."));
        }
        const QString vertical = stringArg(arguments, "vertical").isEmpty() ? QStringLiteral("highest")
                                                                              : stringArg(arguments, "vertical").toLower();
        const QString horizontal = stringArg(arguments, "horizontal").isEmpty() ? QStringLiteral("leftmost")
                                                                                  : stringArg(arguments, "horizontal").toLower();
        if ((vertical != QLatin1String("highest") && vertical != QLatin1String("lowest"))
            || (horizontal != QLatin1String("leftmost") && horizontal != QLatin1String("rightmost")))
        {
            return McpToolResult::error(QStringLiteral("'vertical' must be \"highest\" or \"lowest\" and "
                                                       "'horizontal' \"leftmost\" or \"rightmost\" (which crossing "
                                                       "to take when the curves cross more than once)."));
        }
        vCross = vertical == QLatin1String("highest") ? VCrossCurvesPoint::HighestPoint : VCrossCurvesPoint::LowestPoint;
        hCross = horizontal == QLatin1String("leftmost") ? HCrossCurvesPoint::LeftmostPoint
                                                         : HCrossCurvesPoint::RightmostPoint;
        if (VToolPointOfIntersectionCurves::FindPoint(c1->getPoints(), c2->getPoints(), vCross, hCross).isNull())
        {
            return McpToolResult::error(QStringLiteral("The two curves do not cross."));
        }
    }
    else // intersectXY
    {
        if (!pointArg(ctx, arguments, "x_from_point_id", p1, error)
            || !pointArg(ctx, arguments, "y_from_point_id", p2, error))
        {
            return McpToolResult::error(error);
        }
    }

    // --- create it like the tool dialog does ----------------------------------------------------------------------
    QUndoStack *stack = qApp->getUndoStack();
    stack->beginMacro(QObject::tr("Claude: add point %1").arg(name));
    quint32 newId = NULL_ID;
    try
    {
        const Document parse = Document::FullParse;
        const Source from = Source::FromGui;
        if (type == QLatin1String("height"))
        {
            VToolHeight *t = VToolHeight::Create(0, name, LineTypeSolidLine, DefaultLineWeight, ColorBlack, base, p1, p2,
                                                 5, 10, true, ctx.scene, ctx.doc, ctx.data, parse, from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
        else if (type == QLatin1String("lineIntersectAxis"))
        {
            VToolLineIntersectAxis *t = VToolLineIntersectAxis::Create(0, name, LineTypeSolidLine, DefaultLineWeight,
                                                                       ColorBlack, angle, base, p1, p2, 5, 10, true,
                                                                       ctx.scene, ctx.doc, ctx.data, parse, from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
        else if (type == QLatin1String("curveIntersectAxis"))
        {
            VToolCurveIntersectAxis *t = VToolCurveIntersectAxis::Create(0, name, LineTypeSolidLine, DefaultLineWeight,
                                                                         ColorBlack, angle, base, p1, 5, 10, true,
                                                                         ctx.scene, ctx.doc, ctx.data, parse, from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
        else if (type == QLatin1String("cutCurve"))
        {
            const GOType kind = ctx.data->GetGObject(p1)->getType();
            if (kind == GOType::Arc)
            {
                VToolCutArc *t = VToolCutArc::Create(0, name, direction, length, ColorBlack, p1, 5, 10, true, ctx.scene,
                                                     ctx.doc, ctx.data, parse, from);
                newId = t != nullptr ? t->getId() : NULL_ID;
            }
            else if (kind == GOType::SplinePath || kind == GOType::CubicBezierPath)
            {
                VToolCutSplinePath *t = VToolCutSplinePath::Create(0, name, direction, length, ColorBlack, p1, 5, 10,
                                                                   true, ctx.scene, ctx.doc, ctx.data, parse, from);
                newId = t != nullptr ? t->getId() : NULL_ID;
            }
            else
            {
                VToolCutSpline *t = VToolCutSpline::Create(0, name, direction, length, ColorBlack, p1, 5, 10, true,
                                                           ctx.scene, ctx.doc, ctx.data, parse, from);
                newId = t != nullptr ? t->getId() : NULL_ID;
            }
        }
        else if (type == QLatin1String("shoulder"))
        {
            VToolShoulderPoint *t = VToolShoulderPoint::Create(0, length, p1, p2, p3, LineTypeSolidLine, DefaultLineWeight,
                                                               ColorBlack, name, 5, 10, true, ctx.scene, ctx.doc,
                                                               ctx.data, parse, from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
        else if (type == QLatin1String("triangle"))
        {
            VToolTriangle *t = VToolTriangle::Create(0, name, p1, p2, p3, p4, 5, 10, true, ctx.scene, ctx.doc, ctx.data,
                                                     parse, from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
        else if (type == QLatin1String("intersectCircles"))
        {
            IntersectCirclesTool *t = IntersectCirclesTool::Create(0, name, p1, p2, radius1, radius2, crossing, 5, 10,
                                                                   true, ctx.scene, ctx.doc, ctx.data, parse, from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
        else if (type == QLatin1String("intersectCircleLine"))
        {
            IntersectArcLineTool *t = IntersectArcLineTool::Create(0, radius1, p1, p2, p3, name, 5, 10, true, ctx.scene,
                                                                   ctx.doc, ctx.data, parse, from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
        else if (type == QLatin1String("tangentToCircle"))
        {
            IntersectCircleTangentTool *t = IntersectCircleTangentTool::Create(0, name, p1, radius1, p2, crossing, 5, 10,
                                                                               true, ctx.scene, ctx.doc, ctx.data,
                                                                               parse, from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
        else if (type == QLatin1String("intersectArcs"))
        {
            VToolPointOfIntersectionArcs *t = VToolPointOfIntersectionArcs::Create(0, name, p1, p2, crossing, 5, 10,
                                                                                   ctx.scene, ctx.doc, ctx.data, parse,
                                                                                   from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
        else if (type == QLatin1String("tangentToArc"))
        {
            VToolPointFromArcAndTangent *t = VToolPointFromArcAndTangent::Create(0, name, p1, p2, crossing, 5, 10, true,
                                                                                 ctx.scene, ctx.doc, ctx.data, parse,
                                                                                 from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
        else if (type == QLatin1String("intersectCurves"))
        {
            VToolPointOfIntersectionCurves *t = VToolPointOfIntersectionCurves::Create(0, name, p1, p2, vCross, hCross, 5,
                                                                                       10, true, ctx.scene, ctx.doc,
                                                                                       ctx.data, parse, from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
        else
        {
            PointIntersectXYTool *t = PointIntersectXYTool::Create(0, name, LineTypeSolidLine, DefaultLineWeight,
                                                                    ColorBlack, p1, p2, 5, 10, true, ctx.scene, ctx.doc,
                                                                    ctx.data, parse, from);
            newId = t != nullptr ? t->getId() : NULL_ID;
        }
    }
    catch (const VException &e)
    {
        stack->endMacro();
        return McpToolResult::error(QStringLiteral("Could not create the point: %1").arg(e.ErrorMessage()));
    }
    stack->endMacro();

    if (newId == NULL_ID)
    {
        return McpToolResult::error(QStringLiteral("The point could not be created."));
    }
    const QSharedPointer<VPointF> point = ctx.data->GeometricObject<VPointF>(newId);
    return McpToolResult::ok(QStringLiteral("Added point %1 (id %2). Undo removes it.").arg(point->name()).arg(newId),
                             pointJson(newId, *point));
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::addPoint(const QJsonObject &arguments)
{
    const Context ctx = currentContext();
    if (!ctx.valid())
    {
        return McpToolResult::error(ctx.error);
    }

    const QString type = stringArg(arguments, "type");
    const QStringList supported{VToolEndLine::ToolType, VToolAlongLine::ToolType, VToolNormal::ToolType,
                                VToolBisector::ToolType, VToolLineIntersect::ToolType};
    if (kAdvancedPointTypes.contains(type))
    {
        QString name = stringArg(arguments, "name");
        if (name.isEmpty())
        {
            name = ctx.doc->GenerateLabel(LabelType::NewLabel);
        }
        else if (!VContainer::IsUnique(name))
        {
            return McpToolResult::error(QStringLiteral("The name \"%1\" is already used. Choose another or omit "
                                                       "'name'.").arg(name));
        }
        return addAdvancedPoint(arguments, type, name);
    }
    if (!supported.contains(type))
    {
        return McpToolResult::error(QStringLiteral("Unsupported point type \"%1\". Supported: %2.")
                                        .arg(type, (supported + kAdvancedPointTypes).join(QStringLiteral(", "))));
    }

    // --- validate everything before touching the pattern ---------------------------------------------------------
    QString error;
    QString name = stringArg(arguments, "name");
    if (name.isEmpty())
    {
        name = ctx.doc->GenerateLabel(LabelType::NewLabel);
    }
    else if (!VContainer::IsUnique(name))
    {
        return McpToolResult::error(QStringLiteral("The name \"%1\" is already used. Choose another or omit 'name'.")
                                        .arg(name));
    }

    QHash<QString, QSharedPointer<VInternalVariable>> variables = *ctx.data->DataVariables();
    QString length = stringArg(arguments, "length");
    QString angle  = stringArg(arguments, "angle");
    quint32 p1 = NULL_ID;
    quint32 p2 = NULL_ID;
    quint32 p3 = NULL_ID;
    quint32 p4 = NULL_ID;
    double value = 0;
    double angleValue = 0;

    if (type == VToolEndLine::ToolType)
    {
        if (!pointArg(ctx, arguments, "base_point_id", p1, error)
            || !checkFormula(variables, QStringLiteral("length"), length, value, error)
            || !checkFormula(variables, QStringLiteral("angle"), angle, angleValue, error))
        {
            return McpToolResult::error(error);
        }
    }
    else if (type == VToolAlongLine::ToolType)
    {
        if (!pointArg(ctx, arguments, "base_point_id", p1, error)
            || !pointArg(ctx, arguments, "second_point_id", p2, error))
        {
            return McpToolResult::error(error);
        }
        if (p1 == p2)
        {
            return McpToolResult::error(QStringLiteral("'base_point_id' and 'second_point_id' must differ."));
        }
        // "CurrentLength" (length of the line between both points) may be used in the formula.
        const QSharedPointer<VPointF> first  = ctx.data->GeometricObject<VPointF>(p1);
        const QSharedPointer<VPointF> second = ctx.data->GeometricObject<VPointF>(p2);
        variables.insert(currentLength,
                         QSharedPointer<VInternalVariable>(new VLengthLine(first.data(), p1, second.data(), p2,
                                                                           *ctx.data->GetPatternUnit())));
        if (!checkFormula(variables, QStringLiteral("length"), length, value, error))
        {
            return McpToolResult::error(error);
        }
    }
    else if (type == VToolNormal::ToolType)
    {
        if (!pointArg(ctx, arguments, "base_point_id", p1, error)
            || !pointArg(ctx, arguments, "second_point_id", p2, error))
        {
            return McpToolResult::error(error);
        }
        if (p1 == p2)
        {
            return McpToolResult::error(QStringLiteral("'base_point_id' and 'second_point_id' must differ."));
        }
        if (angle.isEmpty())
        {
            angle = QStringLiteral("0");
        }
        if (!checkFormula(variables, QStringLiteral("length"), length, value, error)
            || !checkFormula(variables, QStringLiteral("angle"), angle, angleValue, error))
        {
            return McpToolResult::error(error);
        }
    }
    else if (type == VToolBisector::ToolType)
    {
        if (!pointArg(ctx, arguments, "first_point_id", p1, error)
            || !pointArg(ctx, arguments, "vertex_point_id", p2, error)
            || !pointArg(ctx, arguments, "third_point_id", p3, error))
        {
            return McpToolResult::error(error);
        }
        if (p1 == p2 || p2 == p3 || p1 == p3)
        {
            return McpToolResult::error(QStringLiteral("'first_point_id', 'vertex_point_id' and 'third_point_id' "
                                                       "must be three different points."));
        }
        if (!checkFormula(variables, QStringLiteral("length"), length, value, error))
        {
            return McpToolResult::error(error);
        }
    }
    else // lineIntersect
    {
        if (!pointArg(ctx, arguments, "line1_point1_id", p1, error)
            || !pointArg(ctx, arguments, "line1_point2_id", p2, error)
            || !pointArg(ctx, arguments, "line2_point1_id", p3, error)
            || !pointArg(ctx, arguments, "line2_point2_id", p4, error))
        {
            return McpToolResult::error(error);
        }
        if (p1 == p2 || p3 == p4)
        {
            return McpToolResult::error(QStringLiteral("Each line needs two different points."));
        }
        const QLineF line1(static_cast<QPointF>(*ctx.data->GeometricObject<VPointF>(p1)),
                           static_cast<QPointF>(*ctx.data->GeometricObject<VPointF>(p2)));
        const QLineF line2(static_cast<QPointF>(*ctx.data->GeometricObject<VPointF>(p3)),
                           static_cast<QPointF>(*ctx.data->GeometricObject<VPointF>(p4)));
        QPointF crossing;
        const QLineF::IntersectType kind = line1.intersects(line2, &crossing);
        if (kind != QLineF::BoundedIntersection && kind != QLineF::UnboundedIntersection)
        {
            return McpToolResult::error(QStringLiteral("The two lines are parallel and do not intersect."));
        }
    }

    // --- create the point exactly like the tool dialog does; one undo step "Claude: ..." --------------------------
    QUndoStack *stack = qApp->getUndoStack();
    stack->beginMacro(QObject::tr("Claude: add point %1").arg(name));
    quint32 newId = NULL_ID;
    try
    {
        const Document parse = Document::FullParse;
        const Source from = Source::FromGui;
        if (type == VToolEndLine::ToolType)
        {
            VToolEndLine *tool = VToolEndLine::Create(0, name, LineTypeSolidLine, DefaultLineWeight, ColorBlack,
                                                      length, angle, p1, 5, 10, true, ctx.scene, ctx.doc, ctx.data,
                                                      parse, from);
            newId = tool != nullptr ? tool->getId() : NULL_ID;
        }
        else if (type == VToolAlongLine::ToolType)
        {
            VToolAlongLine *tool = VToolAlongLine::Create(0, name, LineTypeSolidLine, DefaultLineWeight, ColorBlack,
                                                          length, p1, p2, 5, 10, true, ctx.scene, ctx.doc, ctx.data,
                                                          parse, from);
            newId = tool != nullptr ? tool->getId() : NULL_ID;
        }
        else if (type == VToolNormal::ToolType)
        {
            VToolNormal *tool = VToolNormal::Create(0, length, p1, p2, LineTypeSolidLine, DefaultLineWeight,
                                                    ColorBlack, name, angleValue, 5, 10, true, ctx.scene, ctx.doc,
                                                    ctx.data, parse, from);
            newId = tool != nullptr ? tool->getId() : NULL_ID;
        }
        else if (type == VToolBisector::ToolType)
        {
            VToolBisector *tool = VToolBisector::Create(0, length, p1, p2, p3, LineTypeSolidLine, DefaultLineWeight,
                                                        ColorBlack, name, 5, 10, true, ctx.scene, ctx.doc, ctx.data,
                                                        parse, from);
            newId = tool != nullptr ? tool->getId() : NULL_ID;
        }
        else
        {
            VToolLineIntersect *tool = VToolLineIntersect::Create(0, p1, p2, p3, p4, name, 5, 10, true, ctx.scene,
                                                                  ctx.doc, ctx.data, parse, from);
            newId = tool != nullptr ? tool->getId() : NULL_ID;
        }
    }
    catch (const VException &e)
    {
        stack->endMacro();
        return McpToolResult::error(QStringLiteral("Could not create the point: %1").arg(e.ErrorMessage()));
    }
    stack->endMacro();

    if (newId == NULL_ID)
    {
        return McpToolResult::error(QStringLiteral("The point could not be created."));
    }
    const QSharedPointer<VPointF> point = ctx.data->GeometricObject<VPointF>(newId);
    return McpToolResult::ok(QStringLiteral("Added point %1 (id %2). Undo removes it.").arg(point->name()).arg(newId),
                             pointJson(newId, *point));
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::addLine(const QJsonObject &arguments)
{
    const Context ctx = currentContext();
    if (!ctx.valid())
    {
        return McpToolResult::error(ctx.error);
    }

    QString error;
    quint32 first = NULL_ID;
    quint32 second = NULL_ID;
    if (!pointArg(ctx, arguments, "first_point_id", first, error)
        || !pointArg(ctx, arguments, "second_point_id", second, error))
    {
        return McpToolResult::error(error);
    }
    if (first == second)
    {
        return McpToolResult::error(QStringLiteral("'first_point_id' and 'second_point_id' must differ."));
    }

    // Several point tools already draw a line to their base point; refuse to stack a second, identical line.
    QDomElement calculation;
    if (ctx.doc->getActiveNodeElement(VAbstractPattern::TagCalculation, calculation))
    {
        for (QDomNode node = calculation.firstChild(); !node.isNull(); node = node.nextSibling())
        {
            if (drawsLineBetween(node.toElement(), first, second))
            {
                return McpToolResult::error(QStringLiteral("There is already a line between points %1 and %2.")
                                                .arg(first).arg(second));
            }
        }
    }

    QUndoStack *stack = qApp->getUndoStack();
    stack->beginMacro(QObject::tr("Claude: add line"));
    quint32 newId = NULL_ID;
    try
    {
        VToolLine *tool = VToolLine::Create(0, first, second, LineTypeSolidLine, DefaultLineWeight, ColorBlack,
                                            ctx.scene, ctx.doc, ctx.data, Document::FullParse, Source::FromGui);
        newId = tool != nullptr ? tool->getId() : NULL_ID;
    }
    catch (const VException &e)
    {
        stack->endMacro();
        return McpToolResult::error(QStringLiteral("Could not create the line: %1").arg(e.ErrorMessage()));
    }
    stack->endMacro();

    if (newId == NULL_ID)
    {
        return McpToolResult::error(QStringLiteral("The line could not be created."));
    }
    return McpToolResult::ok(QStringLiteral("Added line between points %1 and %2 (id %3). Undo removes it.")
                                 .arg(first).arg(second).arg(newId),
                             QJsonObject{{QStringLiteral("id"), static_cast<qint64>(newId)},
                                         {QStringLiteral("firstPointId"), static_cast<qint64>(first)},
                                         {QStringLiteral("secondPointId"), static_cast<qint64>(second)}});
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::fitView(const QJsonObject &arguments)
{
    MainWindow *window = qobject_cast<MainWindow *>(qApp->getMainWindow());
    if (window == nullptr)
    {
        return McpToolResult::error(QStringLiteral("No pattern is open in Seamly2D."));
    }
    const QString target = stringArg(arguments, "target").isEmpty() ? QStringLiteral("all") : stringArg(arguments, "target");
    quint32 pieceId = NULL_ID;
    if (target == QLatin1String("piece"))
    {
        const QJsonValue id = arguments.value(QStringLiteral("piece_id"));
        if (!id.isDouble() || id.toDouble() < 1)
        {
            return McpToolResult::error(QStringLiteral("'piece_id' is required for target \"piece\" (see list_pieces)."));
        }
        pieceId = static_cast<quint32>(id.toDouble());
    }
    QString error;
    if (!window->mcpZoom(target, pieceId, error))
    {
        return McpToolResult::error(error);
    }
    static const QHash<QString, QString> what{{QStringLiteral("all"), QStringLiteral("the whole drawing")},
                                              {QStringLiteral("block"), QStringLiteral("the active draft block")},
                                              {QStringLiteral("piece"), QStringLiteral("the piece")}};
    return McpToolResult::ok(QStringLiteral("Zoomed the canvas to %1.").arg(what.value(target)));
}

namespace
{
/// What kind of value an editable attribute holds.
enum class AttrKind { Text, Formula, Number, Reference, CurveReference, Direction };

/// One argument of `update_object`: the tool argument, the attribute it sets in the pattern file and its kind.
struct EditableAttr
{
    const char *argument;
    const char *attribute;
    AttrKind    kind;
};

/// The attributes that `update_object` may change, per element type (point type or "line").
QVector<EditableAttr> editableAttributes(const QDomElement &element)
{
    const QString tag  = element.tagName();
    const QString type = element.attribute(AttrType);
    if (tag == QLatin1String("line"))
    {
        return {{"first_point_id", "firstPoint", AttrKind::Reference},
                {"second_point_id", "secondPoint", AttrKind::Reference}};
    }
    if (tag != QLatin1String("point"))
    {
        return {};
    }
    if (type == VToolEndLine::ToolType)
    {
        return {{"name", "name", AttrKind::Text}, {"length", "length", AttrKind::Formula},
                {"angle", "angle", AttrKind::Formula}, {"base_point_id", "basePoint", AttrKind::Reference}};
    }
    if (type == VToolAlongLine::ToolType)
    {
        return {{"name", "name", AttrKind::Text}, {"length", "length", AttrKind::Formula},
                {"base_point_id", "firstPoint", AttrKind::Reference},
                {"second_point_id", "secondPoint", AttrKind::Reference}};
    }
    if (type == VToolNormal::ToolType)
    {
        return {{"name", "name", AttrKind::Text}, {"length", "length", AttrKind::Formula},
                {"angle", "angle", AttrKind::Number}, {"base_point_id", "firstPoint", AttrKind::Reference},
                {"second_point_id", "secondPoint", AttrKind::Reference}};
    }
    if (type == VToolBisector::ToolType)
    {
        return {{"name", "name", AttrKind::Text}, {"length", "length", AttrKind::Formula},
                {"first_point_id", "firstPoint", AttrKind::Reference},
                {"vertex_point_id", "secondPoint", AttrKind::Reference},
                {"third_point_id", "thirdPoint", AttrKind::Reference}};
    }
    if (type == VToolLineIntersect::ToolType)
    {
        return {{"name", "name", AttrKind::Text}, {"line1_point1_id", "p1Line1", AttrKind::Reference},
                {"line1_point2_id", "p2Line1", AttrKind::Reference},
                {"line2_point1_id", "p1Line2", AttrKind::Reference},
                {"line2_point2_id", "p2Line2", AttrKind::Reference}};
    }
    if (type == QLatin1String("height"))
    {
        return {{"name", "name", AttrKind::Text}, {"base_point_id", "basePoint", AttrKind::Reference},
                {"line_point1_id", "p1Line", AttrKind::Reference}, {"line_point2_id", "p2Line", AttrKind::Reference}};
    }
    if (type == QLatin1String("lineIntersectAxis"))
    {
        return {{"name", "name", AttrKind::Text}, {"angle", "angle", AttrKind::Formula},
                {"base_point_id", "basePoint", AttrKind::Reference}, {"first_point_id", "p1Line", AttrKind::Reference},
                {"second_point_id", "p2Line", AttrKind::Reference}};
    }
    if (type == QLatin1String("curveIntersectAxis"))
    {
        return {{"name", "name", AttrKind::Text}, {"angle", "angle", AttrKind::Formula},
                {"base_point_id", "basePoint", AttrKind::Reference}, {"curve_id", "curve", AttrKind::CurveReference}};
    }
    if (type == QLatin1String("cutSpline") || type == QLatin1String("cutSplinePath") || type == QLatin1String("cutArc"))
    {
        const char *curveAttribute = type == QLatin1String("cutArc") ? "arc"
                                     : type == QLatin1String("cutSplinePath") ? "splinePath" : "spline";
        return {{"name", "name", AttrKind::Text}, {"length", "length", AttrKind::Formula},
                {"from", "direction", AttrKind::Direction}, {"curve_id", curveAttribute, AttrKind::CurveReference}};
    }
    if (type == QLatin1String("shoulder"))
    {
        return {{"name", "name", AttrKind::Text}, {"length", "length", AttrKind::Formula},
                {"line_point1_id", "p1Line", AttrKind::Reference}, {"line_point2_id", "p2Line", AttrKind::Reference},
                {"shoulder_point_id", "pShoulder", AttrKind::Reference}};
    }
    if (type == QLatin1String("triangle"))
    {
        return {{"name", "name", AttrKind::Text}, {"axis_point1_id", "axisP1", AttrKind::Reference},
                {"axis_point2_id", "axisP2", AttrKind::Reference}, {"first_point_id", "firstPoint", AttrKind::Reference},
                {"second_point_id", "secondPoint", AttrKind::Reference}};
    }
    if (type == QLatin1String("intersectXY"))
    {
        return {{"name", "name", AttrKind::Text}, {"x_from_point_id", "firstPoint", AttrKind::Reference},
                {"y_from_point_id", "secondPoint", AttrKind::Reference}};
    }
    if (type == QLatin1String("single"))   // base point of a draft block
    {
        return {{"name", "name", AttrKind::Text}, {"x", "x", AttrKind::Number}, {"y", "y", AttrKind::Number}};
    }
    return {};
}

/// Finds the element with `id` in the calculation section of the active draft block and its position there.
QDomElement findElement(const QDomElement &calculation, quint32 id, int &position)
{
    position = 0;
    for (QDomNode node = calculation.firstChild(); !node.isNull(); node = node.nextSibling(), ++position)
    {
        const QDomElement element = node.toElement();
        if (!element.isNull() && element.attribute(QStringLiteral("id")).toUInt() == id)
        {
            return element;
        }
    }
    return QDomElement();
}

/// Names of the objects that refer to `id` (used to explain why an object cannot be deleted).
QStringList dependentsOf(const QDomElement &calculation, quint32 id)
{
    static const QRegularExpression referenceAttribute(
        QStringLiteral("point|line[12]|arc|curve|center|spline|path|object|axis"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList result;
    for (QDomNode node = calculation.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        const QDomElement element = node.toElement();
        if (element.isNull() || element.attribute(QStringLiteral("id")).toUInt() == id)
        {
            continue;
        }
        const QDomNamedNodeMap attributes = element.attributes();
        for (int i = 0; i < attributes.count(); ++i)
        {
            const QDomAttr attribute = attributes.item(i).toAttr();
            if (attribute.value() == QString::number(id) && attribute.name().contains(referenceAttribute))
            {
                const QString label = element.attribute(QStringLiteral("name"));
                result << (label.isEmpty() ? QStringLiteral("%1 (id %2)").arg(element.tagName(),
                                                                              element.attribute(QStringLiteral("id")))
                                           : QStringLiteral("%1 (id %2)").arg(label,
                                                                              element.attribute(QStringLiteral("id"))));
                break;
            }
        }
    }
    return result;
}
} // namespace

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::deleteObject(const QJsonObject &arguments)
{
    const Context ctx = currentContext();
    if (!ctx.valid())
    {
        return McpToolResult::error(ctx.error);
    }
    const QJsonValue idValue = arguments.value(QStringLiteral("id"));
    if (!idValue.isDouble() || idValue.toDouble() < 1 || idValue.toDouble() != qFloor(idValue.toDouble()))
    {
        return McpToolResult::error(QStringLiteral("'id' is required: the id of the object to delete (see "
                                                   "list_objects)."));
    }
    const quint32 id = static_cast<quint32>(idValue.toDouble());

    QDomElement calculation;
    if (!ctx.doc->getActiveNodeElement(VAbstractPattern::TagCalculation, calculation))
    {
        return McpToolResult::error(QStringLiteral("The active draft block has no calculation section."));
    }
    int position = 0;
    const QDomElement element = findElement(calculation, id, position);
    if (element.isNull())
    {
        return McpToolResult::error(QStringLiteral("No object with id %1 in the active draft block (see "
                                                   "list_objects).").arg(id));
    }
    if (element.tagName() == QLatin1String("point") && element.attribute(AttrType) == QLatin1String("single"))
    {
        return McpToolResult::error(QStringLiteral("Object %1 is the base point of the draft block and cannot be "
                                                   "deleted.").arg(id));
    }
    const QStringList dependents = dependentsOf(calculation, id);
    VAbstractTool *tool = qobject_cast<VAbstractTool *>(VAbstractPattern::getTool(id));
    if (tool == nullptr)
    {
        return McpToolResult::error(QStringLiteral("Object %1 cannot be deleted with this tool.").arg(id));
    }
    if (tool->isUsed() || !dependents.isEmpty())
    {
        return McpToolResult::error(QStringLiteral("Object %1 is still used by: %2. Delete or change those first.")
                                        .arg(id)
                                        .arg(dependents.isEmpty() ? QStringLiteral("other objects")
                                                                  : dependents.join(QStringLiteral(", "))));
    }

    const QString label = element.attribute(QStringLiteral("name")).isEmpty()
                              ? element.tagName() : element.attribute(QStringLiteral("name"));
    QUndoStack *stack = qApp->getUndoStack();
    stack->beginMacro(QObject::tr("Claude: delete %1").arg(label));
    // The same steps as VAbstractTool::deleteTool(), which is protected and would ask for confirmation: clear the
    // selection, then push the undo command that removes the element and triggers a full parse.
    emit qApp->getSceneView()->itemClicked(nullptr);
    DelTool *command = new DelTool(ctx.doc, id);
    QObject::connect(command, &DelTool::NeedFullParsing, ctx.doc, &VAbstractPattern::NeedFullParsing);
    stack->push(command);
    stack->endMacro();

    int ignored = 0;
    if (!findElement(calculation, id, ignored).isNull())
    {
        return McpToolResult::error(QStringLiteral("The object %1 could not be deleted.").arg(id));
    }
    return McpToolResult::ok(QStringLiteral("Deleted %1 (id %2). Undo restores it.").arg(label).arg(id),
                             QJsonObject{{QStringLiteral("id"), static_cast<qint64>(id)}});
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::updateObject(const QJsonObject &arguments)
{
    const Context ctx = currentContext();
    if (!ctx.valid())
    {
        return McpToolResult::error(ctx.error);
    }
    const QJsonValue idValue = arguments.value(QStringLiteral("id"));
    if (!idValue.isDouble() || idValue.toDouble() < 1 || idValue.toDouble() != qFloor(idValue.toDouble()))
    {
        return McpToolResult::error(QStringLiteral("'id' is required: the id of the object to change (see "
                                                   "list_objects)."));
    }
    const quint32 id = static_cast<quint32>(idValue.toDouble());

    QDomElement calculation;
    if (!ctx.doc->getActiveNodeElement(VAbstractPattern::TagCalculation, calculation))
    {
        return McpToolResult::error(QStringLiteral("The active draft block has no calculation section."));
    }
    int position = 0;
    const QDomElement oldElement = findElement(calculation, id, position);
    if (oldElement.isNull())
    {
        return McpToolResult::error(QStringLiteral("No object with id %1 in the active draft block (see "
                                                   "list_objects).").arg(id));
    }
    const QVector<EditableAttr> editable = editableAttributes(oldElement);
    if (editable.isEmpty())
    {
        return McpToolResult::error(QStringLiteral("Objects of this kind cannot be changed with this tool."));
    }

    // Unknown arguments are an error: a typo must not be silently ignored.
    QStringList known{QStringLiteral("id")};
    for (const EditableAttr &attr : editable)
    {
        known << QLatin1String(attr.argument);
    }
    for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it)
    {
        if (!known.contains(it.key()))
        {
            return McpToolResult::error(QStringLiteral("'%1' cannot be changed on this object. Changeable: %2.")
                                            .arg(it.key(), known.mid(1).join(QStringLiteral(", "))));
        }
    }

    // --- validate every change before touching the pattern --------------------------------------------------------
    QHash<QString, QSharedPointer<VInternalVariable>> variables = *ctx.data->DataVariables();
    QMap<QString, QString> changes;      // attribute -> new value
    QString error;
    for (const EditableAttr &attr : editable)
    {
        if (!arguments.contains(QLatin1String(attr.argument)))
        {
            continue;
        }
        const QJsonValue raw = arguments.value(QLatin1String(attr.argument));
        switch (attr.kind)
        {
            case AttrKind::Text:
            {
                const QString text = raw.toString().trimmed();
                if (text.isEmpty())
                {
                    return McpToolResult::error(QStringLiteral("'%1' must not be empty.")
                                                    .arg(QLatin1String(attr.argument)));
                }
                if (text != oldElement.attribute(QLatin1String(attr.attribute)))
                {
                    if (!VContainer::IsUnique(text))
                    {
                        return McpToolResult::error(QStringLiteral("The name \"%1\" is already used.").arg(text));
                    }
                    // Formulas refer to derived variables by point name (e.g. Line_A_B); renaming would break them.
                    const QString old = QRegularExpression::escape(oldElement.attribute(QStringLiteral("name")));
                    const QRegularExpression usedInFormula(QStringLiteral("(_%1(?![A-Za-z0-9]))|((?<![A-Za-z0-9])%1_)")
                                                               .arg(old));
                    for (QDomNode node = calculation.firstChild(); !node.isNull(); node = node.nextSibling())
                    {
                        const QDomElement other = node.toElement();
                        const QDomNamedNodeMap attributes = other.attributes();
                        for (int i = 0; !other.isNull() && i < attributes.count(); ++i)
                        {
                            const QDomAttr a = attributes.item(i).toAttr();
                            if (a.name() != QLatin1String("name") && a.value().contains(usedInFormula))
                            {
                                return McpToolResult::error(
                                    QStringLiteral("Renaming would break a formula that refers to \"%1\" (%2=\"%3\").")
                                        .arg(oldElement.attribute(QStringLiteral("name")), a.name(), a.value()));
                            }
                        }
                    }
                }
                changes.insert(QLatin1String(attr.attribute), text);
                break;
            }
            case AttrKind::Formula:
            case AttrKind::Number:
            {
                const QString formula = raw.isDouble() ? QString::number(raw.toDouble()) : raw.toString().trimmed();
                double value = 0;
                // "CurrentLength" is valid in the length of an alongLine point.
                QHash<QString, QSharedPointer<VInternalVariable>> scope = variables;
                if (oldElement.attribute(AttrType) == VToolAlongLine::ToolType)
                {
                    const quint32 a = arguments.contains(QStringLiteral("base_point_id"))
                                          ? static_cast<quint32>(arguments.value(QStringLiteral("base_point_id")).toDouble())
                                          : oldElement.attribute(AttrFirstPoint).toUInt();
                    const quint32 b = arguments.contains(QStringLiteral("second_point_id"))
                                          ? static_cast<quint32>(arguments.value(QStringLiteral("second_point_id")).toDouble())
                                          : oldElement.attribute(AttrSecondPoint).toUInt();
                    try
                    {
                        const QSharedPointer<VPointF> pa = ctx.data->GeometricObject<VPointF>(a);
                        const QSharedPointer<VPointF> pb = ctx.data->GeometricObject<VPointF>(b);
                        scope.insert(currentLength,
                                     QSharedPointer<VInternalVariable>(new VLengthLine(pa.data(), a, pb.data(), b,
                                                                                       *ctx.data->GetPatternUnit())));
                    }
                    catch (const VException &)
                    {
                        // reported below by the reference check
                    }
                }
                if (!checkFormula(scope, QLatin1String(attr.argument), formula, value, error))
                {
                    return McpToolResult::error(error);
                }
                // Number attributes (x, y, the extra angle of a normal) are stored as plain numbers.
                changes.insert(QLatin1String(attr.attribute),
                               attr.kind == AttrKind::Number ? QString::number(value, 'g', 12) : formula);
                break;
            }
            case AttrKind::Reference:
            {
                quint32 refId = NULL_ID;
                if (!pointArg(ctx, arguments, attr.argument, refId, error))
                {
                    return McpToolResult::error(error);
                }
                int refPosition = 0;
                if (refId == id)
                {
                    return McpToolResult::error(QStringLiteral("'%1' cannot refer to the object itself.")
                                                    .arg(QLatin1String(attr.argument)));
                }
                // An object may only use objects that come before it (a later one would be a circular dependency).
                if (findElement(calculation, refId, refPosition).isNull() || refPosition >= position)
                {
                    return McpToolResult::error(QStringLiteral("'%1' = %2 comes after object %3; an object can only "
                                                               "refer to earlier objects.")
                                                    .arg(QLatin1String(attr.argument)).arg(refId).arg(id));
                }
                changes.insert(QLatin1String(attr.attribute), QString::number(refId));
                break;
            }
            case AttrKind::CurveReference:
            {
                const QJsonValue curveValue = arguments.value(QLatin1String(attr.argument));
                const quint32 refId = curveValue.isDouble() ? static_cast<quint32>(curveValue.toDouble()) : NULL_ID;
                QSharedPointer<VAbstractCurve> curve;
                try
                {
                    curve = ctx.data->GeometricObject<VAbstractCurve>(refId);
                }
                catch (const VException &)
                {
                }
                int refPosition = 0;
                if (curve.isNull() || findElement(calculation, refId, refPosition).isNull())
                {
                    return McpToolResult::error(QStringLiteral("'%1' must be the id of a curve or arc of the active "
                                                               "draft block.").arg(QLatin1String(attr.argument)));
                }
                if (refPosition >= position)
                {
                    return McpToolResult::error(QStringLiteral("'%1' = %2 comes after object %3; an object can only "
                                                               "refer to earlier objects.")
                                                    .arg(QLatin1String(attr.argument)).arg(refId).arg(id));
                }
                changes.insert(QLatin1String(attr.attribute), QString::number(refId));
                break;
            }
            case AttrKind::Direction:
            {
                const QString from = raw.toString().trimmed().toLower();
                if (from != QLatin1String("start") && from != QLatin1String("end"))
                {
                    return McpToolResult::error(QStringLiteral("'from' must be \"start\" or \"end\"."));
                }
                changes.insert(QLatin1String(attr.attribute), from == QLatin1String("start") ? QStringLiteral("forward")
                                                                                            : QStringLiteral("backward"));
                break;
            }
        }
    }
    if (changes.isEmpty())
    {
        return McpToolResult::error(QStringLiteral("Nothing to change: give at least one of: %1.")
                                        .arg(known.mid(1).join(QStringLiteral(", "))));
    }

    // --- apply: same undo command as the tool dialogs ------------------------------------------------------------
    QDomElement newElement = oldElement.cloneNode().toElement();
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it)
    {
        ctx.doc->SetAttribute(newElement, it.key(), it.value());
    }
    const QString label = oldElement.attribute(QStringLiteral("name")).isEmpty()
                              ? oldElement.tagName() : oldElement.attribute(QStringLiteral("name"));
    QUndoStack *stack = qApp->getUndoStack();
    stack->beginMacro(QObject::tr("Claude: change %1").arg(label));
    SaveToolOptions *save = new SaveToolOptions(oldElement, newElement, ctx.doc, id);
    QObject::connect(save, &SaveToolOptions::NeedLiteParsing, ctx.doc, &VAbstractPattern::LiteParseTree);
    stack->push(save);
    stack->endMacro();

    QJsonObject result{{QStringLiteral("id"), static_cast<qint64>(id)}};
    try
    {
        if (ctx.data->GetGObject(id)->getType() == GOType::Point)
        {
            result = pointJson(id, *ctx.data->GeometricObject<VPointF>(id));
        }
    }
    catch (const VException &)
    {
        // objects without a position (lines) are reported by id only
    }
    return McpToolResult::ok(QStringLiteral("Changed %1 (id %2). Undo restores the old values.").arg(label).arg(id),
                             result);
}

namespace
{
/// Internal name of a custom variable ("increment") as used in formulas: "#" + name.
QString internalVariableName(const QString &name)
{
    return CustomIncrSign + name;
}

/// Variables are referenced in formulas as #name.
const QRegularExpression &variableReference()
{
    static const QRegularExpression expression(QStringLiteral("#[\\p{L}_][\\p{L}\\p{N}_]*"));
    return expression;
}

QStringList referencesIn(const QString &formula)
{
    QStringList result;
    QRegularExpressionMatchIterator it = variableReference().globalMatch(formula);
    while (it.hasNext())
    {
        result << it.next().captured();
    }
    return result;
}

/// True if giving `name` the formula `formula` would make the variables depend on each other in a circle.
bool createsCycle(const VContainer *data, const QString &name, const QString &formula)
{
    QMap<QString, QStringList> graph;
    const QMap<QString, QSharedPointer<CustomVariable>> all = data->variablesData();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
    {
        graph.insert(it.key(), referencesIn(it.value()->GetFormula()));
    }
    graph.insert(name, referencesIn(formula));

    QStringList stack = graph.value(name);
    QSet<QString> seen;
    while (!stack.isEmpty())
    {
        const QString current = stack.takeLast();
        if (current == name)
        {
            return true;
        }
        if (!seen.contains(current))
        {
            seen.insert(current);
            stack << graph.value(current);
        }
    }
    return false;
}

/// State of one custom variable, for undo.
struct VariableState
{
    bool    exists = false;
    QString formula;
    QString description;
    int     index = 0;      ///< position among the variables
};

/// Create / change / delete a custom variable as one undo step. The variables dialog edits the pattern directly
/// without the undo stack, so this command does the same edits through VPattern and re-parses like the dialog does.
class VariableCommand : public QUndoCommand
{
public:
    VariableCommand(VPattern *doc, const QString &name, const VariableState &before, const VariableState &after,
                    const QString &text)
        : QUndoCommand(text), m_doc(doc), m_name(name), m_before(before), m_after(after)
    {}

    void redo() override { apply(m_after, m_before); }
    void undo() override { apply(m_before, m_after); }

private:
    void apply(const VariableState &target, const VariableState &current)
    {
        if (!target.exists)
        {
            if (current.exists)
            {
                m_doc->removeCustomVariable(m_name);
            }
        }
        else
        {
            if (!current.exists)
            {
                m_doc->addEmptyCustomVariable(m_name);   // appended; move it back to its old position
                const int count = m_doc->elementsByTagName(VAbstractPattern::TagVariables).at(0).childNodes().count();
                for (int i = count - 1; i > target.index; --i)
                {
                    m_doc->moveVariableUp(m_name);
                }
            }
            m_doc->setVariableFormula(m_name, target.formula);
            m_doc->setVariableDescription(m_name, target.description);
        }
        m_doc->LiteParseVariables();
        m_doc->LiteParseTree(Document::LiteParse);
    }

    VPattern      *m_doc;
    QString        m_name;
    VariableState  m_before;
    VariableState  m_after;
};

/// The variable's state as stored now (exists == false if there is none).
VariableState currentState(const VContainer *data, const QString &internalName)
{
    VariableState state;
    const QMap<QString, QSharedPointer<CustomVariable>> all = data->variablesData();
    int index = 0;
    // Variables are kept in file order by their index.
    QMap<quint32, QString> byIndex;
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
    {
        byIndex.insert(it.value()->getIndex(), it.key());
    }
    for (auto it = byIndex.constBegin(); it != byIndex.constEnd(); ++it, ++index)
    {
        if (it.value() == internalName)
        {
            const QSharedPointer<CustomVariable> variable = all.value(internalName);
            state.exists = true;
            state.formula = variable->GetFormula();
            state.description = variable->GetDescription();
            state.index = index;
            break;
        }
    }
    if (!state.exists)
    {
        state.index = all.size();
    }
    return state;
}

/// Validates a variable name given without the leading "#". Returns an empty string if it is fine.
QString checkVariableName(const QString &name)
{
    static const QRegularExpression valid(QStringLiteral("^[\\p{L}_][\\p{L}\\p{N}_]*$"));
    if (!name.contains(valid))
    {
        return QStringLiteral("The variable name \"%1\" is not valid: use letters, digits and underscores, starting "
                              "with a letter or underscore, and do not type the leading #.").arg(name);
    }
    return QString();
}

VPattern *currentPattern()
{
    return qobject_cast<VPattern *>(qApp->getCurrentDocument());
}
} // namespace

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::listVariables(const QJsonObject &arguments)
{
    Q_UNUSED(arguments)
    VContainer *data = qApp->getCurrentData();
    if (data == nullptr || currentPattern() == nullptr)
    {
        return McpToolResult::error(QStringLiteral("No pattern is open in Seamly2D."));
    }

    QJsonArray variables;
    const QMap<QString, QSharedPointer<CustomVariable>> all = data->variablesData();
    QMap<quint32, QString> byIndex;
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
    {
        byIndex.insert(it.value()->getIndex(), it.key());
    }
    for (auto it = byIndex.constBegin(); it != byIndex.constEnd(); ++it)
    {
        const QSharedPointer<CustomVariable> variable = all.value(it.value());
        variables.append(QJsonObject{{QStringLiteral("name"), variable->GetName().mid(CustomIncrSign.size())},
                                     {QStringLiteral("reference"), variable->GetName()},
                                     {QStringLiteral("formula"), variable->GetFormula()},
                                     {QStringLiteral("value"), round3(*variable->GetValue())},
                                     {QStringLiteral("valid"), variable->IsFormulaOk()},
                                     {QStringLiteral("description"), variable->GetDescription()}});
    }

    QJsonArray measurements;
    const QHash<QString, QSharedPointer<VInternalVariable>> *all2 = data->DataVariables();
    QStringList names;
    for (auto it = all2->constBegin(); it != all2->constEnd(); ++it)
    {
        if (it.value()->GetType() == VarType::Measurement)
        {
            names << it.key();
        }
    }
    names.sort();
    for (const QString &name : names)
    {
        measurements.append(QJsonObject{{QStringLiteral("name"), name},
                                        {QStringLiteral("value"), round3(static_cast<const VInternalVariable *>(all2->value(name).data())->GetValue())}});
    }

    return McpToolResult::ok(QStringLiteral("%1 variable(s), %2 measurement(s) (unit: %3). In formulas write a "
                                            "variable as its 'reference' (#name) and a measurement by its name.")
                                 .arg(variables.size()).arg(measurements.size(), 0, 10)
                                 .arg(UnitsToStr(qApp->patternUnit())),
                             QJsonObject{{QStringLiteral("unit"), UnitsToStr(qApp->patternUnit())},
                                         {QStringLiteral("variables"), variables},
                                         {QStringLiteral("measurements"), measurements}});
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::setVariable(const QJsonObject &arguments)
{
    VPattern *doc = currentPattern();
    VContainer *data = qApp->getCurrentData();
    if (doc == nullptr || data == nullptr)
    {
        return McpToolResult::error(QStringLiteral("No pattern is open in Seamly2D."));
    }

    const QString name = stringArg(arguments, "name");
    const QString nameError = checkVariableName(name);
    if (!nameError.isEmpty())
    {
        return McpToolResult::error(nameError);
    }
    const QString internal = internalVariableName(name);
    const VariableState before = currentState(data, internal);

    // A new variable needs a formula; an existing one keeps what is not given.
    QString formula = stringArg(arguments, "formula");
    if (arguments.value(QStringLiteral("formula")).isDouble())
    {
        formula = QString::number(arguments.value(QStringLiteral("formula")).toDouble());
    }
    if (formula.isEmpty())
    {
        if (!before.exists)
        {
            return McpToolResult::error(QStringLiteral("'formula' is required for a new variable (a number or a "
                                                       "formula such as \"waist/4\")."));
        }
        formula = before.formula;
    }
    if (!before.exists && !VContainer::IsUnique(internal))
    {
        return McpToolResult::error(QStringLiteral("The name \"%1\" is already used.").arg(name));
    }
    QString description = before.description;
    if (arguments.contains(QStringLiteral("description")))
    {
        description = arguments.value(QStringLiteral("description")).toString();
    }

    QString error;
    double value = 0;
    // Evaluate against the existing variables, but never against the variable's own current value.
    QHash<QString, QSharedPointer<VInternalVariable>> variables = *data->DataVariables();
    variables.remove(internal);
    if (!checkFormula(variables, QStringLiteral("formula"), formula, value, error))
    {
        return McpToolResult::error(error);
    }
    if (createsCycle(data, internal, formula))
    {
        return McpToolResult::error(QStringLiteral("The formula would make \"%1\" depend on itself (directly or "
                                                   "through other variables).").arg(name));
    }

    VariableState after;
    after.exists = true;
    after.formula = formula;
    after.description = description;
    after.index = before.index;

    QUndoStack *stack = qApp->getUndoStack();
    stack->push(new VariableCommand(doc, internal, before, after,
                                    QObject::tr("Claude: %1 variable %2").arg(before.exists ? QObject::tr("change")
                                                                                            : QObject::tr("add"), name)));

    // Read the result back from the freshly parsed data.
    const QSharedPointer<CustomVariable> variable = qApp->getCurrentData()->getVariable<CustomVariable>(internal);
    return McpToolResult::ok(QStringLiteral("%1 variable %2 = %3 (formula \"%4\"). Use it in formulas as %5. Undo "
                                            "restores the previous state.")
                                 .arg(before.exists ? QStringLiteral("Changed") : QStringLiteral("Added"), name)
                                 .arg(round3(*variable->GetValue())).arg(formula, internal),
                             QJsonObject{{QStringLiteral("name"), name},
                                         {QStringLiteral("reference"), internal},
                                         {QStringLiteral("formula"), formula},
                                         {QStringLiteral("value"), round3(*variable->GetValue())},
                                         {QStringLiteral("description"), description}});
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::deleteVariable(const QJsonObject &arguments)
{
    VPattern *doc = currentPattern();
    VContainer *data = qApp->getCurrentData();
    if (doc == nullptr || data == nullptr)
    {
        return McpToolResult::error(QStringLiteral("No pattern is open in Seamly2D."));
    }
    const QString name = stringArg(arguments, "name");
    const QString internal = internalVariableName(name);
    const VariableState before = currentState(data, internal);
    if (!before.exists)
    {
        return McpToolResult::error(QStringLiteral("There is no variable named \"%1\" (see list_variables).")
                                        .arg(name));
    }

    // Refuse while any formula still uses it: tools and other variables.
    const QRegularExpression used(QRegularExpression::escape(internal) + QStringLiteral("(?![\\p{L}\\p{N}_])"));
    QStringList places;
    const QVector<VFormulaField> expressions = doc->ListExpressions();
    for (const VFormulaField &field : expressions)
    {
        // Variables are checked separately below.
        if (field.element.tagName() != QLatin1String("variable") && field.expression.contains(used))
        {
            const QString label = field.element.attribute(QStringLiteral("name")).isEmpty()
                                      ? field.element.tagName() : field.element.attribute(QStringLiteral("name"));
            places << QStringLiteral("%1 (id %2, %3)").arg(label, field.element.attribute(QStringLiteral("id")),
                                                          field.attribute);
        }
    }
    const QMap<QString, QSharedPointer<CustomVariable>> all = data->variablesData();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
    {
        if (it.key() != internal && it.value()->GetFormula().contains(used))
        {
            places << QStringLiteral("variable %1").arg(it.value()->GetName());
        }
    }
    if (!places.isEmpty())
    {
        places.removeDuplicates();
        return McpToolResult::error(QStringLiteral("The variable \"%1\" is still used by: %2. Change those first.")
                                        .arg(name, places.join(QStringLiteral(", "))));
    }

    VariableState after;   // exists == false
    after.index = before.index;
    qApp->getUndoStack()->push(new VariableCommand(doc, internal, before, after,
                                                   QObject::tr("Claude: delete variable %1").arg(name)));
    return McpToolResult::ok(QStringLiteral("Deleted variable %1. Undo restores it.").arg(name),
                             QJsonObject{{QStringLiteral("name"), name}});
}

namespace
{
/// Reports a freshly created curve or arc: id, name and length in the pattern unit.
QJsonObject curveJson(VContainer *data, quint32 id, const QString &kind)
{
    QJsonObject result{{QStringLiteral("id"), static_cast<qint64>(id)}, {QStringLiteral("kind"), kind}};
    const QSharedPointer<VGObject> object = data->GetGObject(id);
    if (!object.isNull())
    {
        result.insert(QStringLiteral("name"), object->name());
    }
    const QSharedPointer<VAbstractCurve> curve = data->GeometricObject<VAbstractCurve>(id);
    if (!curve.isNull())
    {
        result.insert(QStringLiteral("length"), round3(qApp->fromPixel(curve->GetLength())));
    }
    return result;
}
} // namespace

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::addPathCurve(const QJsonObject &arguments)
{
    const Context ctx = currentContext();
    if (!ctx.valid())
    {
        return McpToolResult::error(ctx.error);
    }
    const QJsonValue pointsValue = arguments.value(QStringLiteral("points"));
    if (!pointsValue.isArray() || pointsValue.toArray().size() < 3)
    {
        return McpToolResult::error(QStringLiteral("'points' must be an array with at least 3 entries, in the order the "
                                                   "curve runs: point ids, or {\"point_id\":..., \"angle\":..., "
                                                   "\"length_in\":..., \"length_out\":...} (use type spline for two "
                                                   "points)."));
    }
    const QJsonArray entries = pointsValue.toArray();
    const int n = entries.size();

    // --- read and check the points ---------------------------------------------------------------------------------
    QVector<quint32> ids;
    QVector<QPointF> positions;
    QSet<quint32> seen;
    QString error;
    for (int i = 0; i < n; ++i)
    {
        const QJsonValue entry = entries.at(i);
        QJsonObject item = entry.isObject() ? entry.toObject() : QJsonObject{{QStringLiteral("point_id"), entry}};
        quint32 id = NULL_ID;
        if (!pointArg(ctx, item, "point_id", id, error))
        {
            return McpToolResult::error(QStringLiteral("points[%1]: %2").arg(i).arg(error));
        }
        if (seen.contains(id))
        {
            return McpToolResult::error(QStringLiteral("points[%1]: point %2 appears twice.").arg(i).arg(id));
        }
        seen.insert(id);
        ids << id;
        positions << positionOf(ctx, id);
    }

    // --- tangent directions and handle lengths: smooth defaults, overridable per point -------------------------------
    // angle1 / angle2 are the directions of the incoming / outgoing handle seen from the point (they point against and
    // along the direction of travel); length1 / length2 are the handle lengths.
    QVector<QString> a1(n), a2(n), l1(n), l2(n);
    const QHash<QString, QSharedPointer<VInternalVariable>> variables = *ctx.data->DataVariables();
    for (int i = 0; i < n; ++i)
    {
        const QJsonObject item = entries.at(i).isObject() ? entries.at(i).toObject() : QJsonObject();
        const QPointF previous = positions.at(qMax(0, i - 1));
        const QPointF next = positions.at(qMin(n - 1, i + 1));
        const double toPrevious = qApp->fromPixel(QLineF(positions.at(i), previous).length());
        const double toNext = qApp->fromPixel(QLineF(positions.at(i), next).length());
        QString direction = formulaArg(item, "angle");
        QString lengthIn = formulaArg(item, "length_in");
        QString lengthOut = formulaArg(item, "length_out");
        double value = 0;
        double directionValue = 0;
        const bool givenAngle = !direction.isEmpty();
        if (!givenAngle)
        {
            directionValue = QLineF(previous, next).angle();   // along the chord from the previous to the next point
            direction = QString::number(directionValue, 'f', 3);
        }
        else if (!checkFormula(variables, QStringLiteral("points[%1].angle").arg(i), direction, directionValue, error))
        {
            return McpToolResult::error(error);
        }
        if (lengthIn.isEmpty())
        {
            lengthIn = QString::number(toPrevious / 3.0, 'f', 3);
        }
        else if (!checkFormula(variables, QStringLiteral("points[%1].length_in").arg(i), lengthIn, value, error))
        {
            return McpToolResult::error(error);
        }
        else if (value < 0)
        {
            return McpToolResult::error(QStringLiteral("points[%1].length_in must not be negative.").arg(i));
        }
        if (lengthOut.isEmpty())
        {
            lengthOut = QString::number(toNext / 3.0, 'f', 3);
        }
        else if (!checkFormula(variables, QStringLiteral("points[%1].length_out").arg(i), lengthOut, value, error))
        {
            return McpToolResult::error(error);
        }
        else if (value < 0)
        {
            return McpToolResult::error(QStringLiteral("points[%1].length_out must not be negative.").arg(i));
        }
        a2[i] = direction;   // outgoing handle: along the direction of travel
        a1[i] = givenAngle ? QStringLiteral("(%1)+180").arg(direction)                  // incoming: the opposite way
                           : QString::number(std::fmod(directionValue + 180.0, 360.0), 'f', 3);
        l1[i] = lengthIn;
        l2[i] = lengthOut;
    }

    QUndoStack *stack = qApp->getUndoStack();
    stack->beginMacro(QObject::tr("Claude: add curve"));
    quint32 newId = NULL_ID;
    try
    {
        VToolSplinePath *tool = VToolSplinePath::Create(0, ids, a1, a2, l1, l2, ColorBlack, LineTypeSolidLine,
                                                        DefaultLineWeight, 0, ctx.scene, ctx.doc, ctx.data,
                                                        Document::FullParse, Source::FromGui);
        newId = tool != nullptr ? tool->getId() : NULL_ID;
    }
    catch (const VException &e)
    {
        stack->endMacro();
        return McpToolResult::error(QStringLiteral("Could not create the curve: %1").arg(e.ErrorMessage()));
    }
    stack->endMacro();
    if (newId == NULL_ID)
    {
        return McpToolResult::error(QStringLiteral("The curve could not be created."));
    }
    QJsonObject result = curveJson(ctx.data, newId, QStringLiteral("path"));
    result.insert(QStringLiteral("points"), n);
    return McpToolResult::ok(QStringLiteral("Added curve %1 through %2 points (id %3, length %4 %5). Undo removes it.")
                                 .arg(result.value(QStringLiteral("name")).toString()).arg(n).arg(newId)
                                 .arg(result.value(QStringLiteral("length")).toDouble())
                                 .arg(UnitsToStr(qApp->patternUnit())),
                             result);
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::addCurve(const QJsonObject &arguments)
{
    const Context ctx = currentContext();
    if (!ctx.valid())
    {
        return McpToolResult::error(ctx.error);
    }

    // "spline" is the name used in the tool description; Seamly2D stores it as "simpleInteractive".
    QString type = stringArg(arguments, "type");
    if (type == VToolSpline::ToolType)
    {
        type = QStringLiteral("spline");
    }
    const QStringList supported{QStringLiteral("spline"), VToolCubicBezier::ToolType, QStringLiteral("path")};
    if (type == QLatin1String("path"))
    {
        return addPathCurve(arguments);
    }
    if (!supported.contains(type))
    {
        return McpToolResult::error(QStringLiteral("Unsupported curve type \"%1\". Supported: %2.")
                                        .arg(type, supported.join(QStringLiteral(", "))));
    }

    QString error;
    quint32 p1 = NULL_ID;
    quint32 p2 = NULL_ID;
    quint32 p3 = NULL_ID;
    quint32 p4 = NULL_ID;
    QString angle1 = stringArg(arguments, "angle1");
    QString angle2 = stringArg(arguments, "angle2");
    QString length1 = stringArg(arguments, "length1");
    QString length2 = stringArg(arguments, "length2");
    const QHash<QString, QSharedPointer<VInternalVariable>> variables = *ctx.data->DataVariables();
    double value = 0;

    if (type == QStringLiteral("spline"))
    {
        if (!pointArg(ctx, arguments, "start_point_id", p1, error)
            || !pointArg(ctx, arguments, "end_point_id", p4, error))
        {
            return McpToolResult::error(error);
        }
        if (p1 == p4)
        {
            return McpToolResult::error(QStringLiteral("'start_point_id' and 'end_point_id' must differ."));
        }
        if (!checkFormula(variables, QStringLiteral("angle1"), angle1, value, error)
            || !checkFormula(variables, QStringLiteral("angle2"), angle2, value, error)
            || !checkFormula(variables, QStringLiteral("length1"), length1, value, error)
            || !checkFormula(variables, QStringLiteral("length2"), length2, value, error))
        {
            return McpToolResult::error(error);
        }
    }
    else // cubicBezier
    {
        if (!pointArg(ctx, arguments, "start_point_id", p1, error)
            || !pointArg(ctx, arguments, "control1_point_id", p2, error)
            || !pointArg(ctx, arguments, "control2_point_id", p3, error)
            || !pointArg(ctx, arguments, "end_point_id", p4, error))
        {
            return McpToolResult::error(error);
        }
        if (p1 == p4)
        {
            return McpToolResult::error(QStringLiteral("'start_point_id' and 'end_point_id' must differ."));
        }
    }

    QUndoStack *stack = qApp->getUndoStack();
    stack->beginMacro(QObject::tr("Claude: add curve"));
    quint32 newId = NULL_ID;
    try
    {
        if (type == QStringLiteral("spline"))
        {
            VToolSpline *tool = VToolSpline::Create(0, p1, p4, angle1, angle2, length1, length2, 0, ColorBlack,
                                                    LineTypeSolidLine, DefaultLineWeight, ctx.scene, ctx.doc,
                                                    ctx.data, Document::FullParse, Source::FromGui);
            newId = tool != nullptr ? tool->getId() : NULL_ID;
        }
        else
        {
            VCubicBezier *bezier = new VCubicBezier(*ctx.data->GeometricObject<VPointF>(p1),
                                                    *ctx.data->GeometricObject<VPointF>(p2),
                                                    *ctx.data->GeometricObject<VPointF>(p3),
                                                    *ctx.data->GeometricObject<VPointF>(p4));
            bezier->setLineColor(ColorBlack);
            bezier->SetPenStyle(LineTypeSolidLine);
            bezier->setLineWeight(DefaultLineWeight);
            VToolCubicBezier *tool = VToolCubicBezier::Create(0, bezier, false, 0, QString(), ctx.scene, ctx.doc,
                                                              ctx.data, Document::FullParse, Source::FromGui);
            newId = tool != nullptr ? tool->getId() : NULL_ID;
        }
    }
    catch (const VException &e)
    {
        stack->endMacro();
        return McpToolResult::error(QStringLiteral("Could not create the curve: %1").arg(e.ErrorMessage()));
    }
    stack->endMacro();

    if (newId == NULL_ID)
    {
        return McpToolResult::error(QStringLiteral("The curve could not be created."));
    }
    const QJsonObject result = curveJson(ctx.data, newId, type);
    return McpToolResult::ok(QStringLiteral("Added curve %1 (id %2, length %3 %4). Undo removes it.")
                                 .arg(result.value(QStringLiteral("name")).toString()).arg(newId)
                                 .arg(result.value(QStringLiteral("length")).toDouble())
                                 .arg(UnitsToStr(qApp->patternUnit())),
                             result);
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::addArc(const QJsonObject &arguments)
{
    const Context ctx = currentContext();
    if (!ctx.valid())
    {
        return McpToolResult::error(ctx.error);
    }

    QString error;
    quint32 center = NULL_ID;
    if (!pointArg(ctx, arguments, "center_point_id", center, error))
    {
        return McpToolResult::error(error);
    }
    QString radius = stringArg(arguments, "radius");
    QString startAngle = stringArg(arguments, "start_angle");
    QString endAngle = stringArg(arguments, "end_angle");
    const QHash<QString, QSharedPointer<VInternalVariable>> variables = *ctx.data->DataVariables();
    double radiusValue = 0;
    double ignored = 0;
    if (!checkFormula(variables, QStringLiteral("radius"), radius, radiusValue, error)
        || !checkFormula(variables, QStringLiteral("start_angle"), startAngle, ignored, error)
        || !checkFormula(variables, QStringLiteral("end_angle"), endAngle, ignored, error))
    {
        return McpToolResult::error(error);
    }
    if (radiusValue <= 0)
    {
        return McpToolResult::error(QStringLiteral("'radius' must be greater than 0 (it is %1).").arg(radiusValue));
    }

    QUndoStack *stack = qApp->getUndoStack();
    stack->beginMacro(QObject::tr("Claude: add arc"));
    quint32 newId = NULL_ID;
    try
    {
        VToolArc *tool = VToolArc::Create(0, center, radius, startAngle, endAngle, ColorBlack, LineTypeSolidLine,
                                          DefaultLineWeight, ctx.scene, ctx.doc, ctx.data, Document::FullParse,
                                          Source::FromGui);
        newId = tool != nullptr ? tool->getId() : NULL_ID;
    }
    catch (const VException &e)
    {
        stack->endMacro();
        return McpToolResult::error(QStringLiteral("Could not create the arc: %1").arg(e.ErrorMessage()));
    }
    stack->endMacro();

    if (newId == NULL_ID)
    {
        return McpToolResult::error(QStringLiteral("The arc could not be created."));
    }
    const QJsonObject result = curveJson(ctx.data, newId, QStringLiteral("arc"));
    return McpToolResult::ok(QStringLiteral("Added arc %1 (id %2, length %3 %4). Undo removes it.")
                                 .arg(result.value(QStringLiteral("name")).toString()).arg(newId)
                                 .arg(result.value(QStringLiteral("length")).toDouble())
                                 .arg(UnitsToStr(qApp->patternUnit())),
                             result);
}

namespace
{
/// VAbstractTool::PrepareNodes() is protected static; a derived struct that is never instantiated may expose it.
struct ToolAccess : VAbstractTool
{
    using VAbstractTool::PrepareNodes;
};

/// Piece node type for a draft object, or Tool::LAST_ONE_DO_NOT_USE if the object cannot be part of a piece outline.
Tool nodeTypeFor(GOType type)
{
    switch (type)
    {
        case GOType::Point:           return Tool::NodePoint;
        case GOType::Arc:             return Tool::NodeArc;
        case GOType::EllipticalArc:   return Tool::NodeElArc;
        case GOType::Spline:
        case GOType::CubicBezier:     return Tool::NodeSpline;
        case GOType::SplinePath:
        case GOType::CubicBezierPath: return Tool::NodeSplinePath;
        default:                      return Tool::LAST_ONE_DO_NOT_USE;
    }
}

double polygonArea(const QVector<QPointF> &points)
{
    double twice = 0;
    for (int i = 0; i < points.size(); ++i)
    {
        const QPointF &a = points.at(i);
        const QPointF &b = points.at((i + 1) % points.size());
        twice += a.x() * b.y() - b.x() * a.y();
    }
    return qAbs(twice) / 2.0;
}

double polygonPerimeter(const QVector<QPointF> &points)
{
    double length = 0;
    for (int i = 0; i < points.size(); ++i)
    {
        length += QLineF(points.at(i), points.at((i + 1) % points.size())).length();
    }
    return length;
}

/// Names used for the notch types in the tools; the index is the NotchType value.
const QStringList kNotchTypes{QStringLiteral("slit"), QStringLiteral("t"), QStringLiteral("v_internal"),
                                  QStringLiteral("v_external"), QStringLiteral("u"), QStringLiteral("castle"),
                                  QStringLiteral("diamond")};

QJsonObject pieceJson(const VContainer *data, quint32 id, const VPiece &piece)
{
    QJsonObject result{{QStringLiteral("id"), static_cast<qint64>(id)},
                       {QStringLiteral("name"), piece.GetName()},
                       {QStringLiteral("nodes"), piece.GetPath().nodeCount()},
                       {QStringLiteral("seamAllowance"), piece.hasSeamAllowance()},
                       {QStringLiteral("seamAllowanceWidth"), round3(piece.GetSAWidth())}};
    // A piece can belong to another draft block than the active one; its tool keeps the data it needs (this is also
    // what the layout code uses).
    VAbstractTool *tool = qobject_cast<VAbstractTool *>(VAbstractPattern::getTool(id));
    const VContainer *pieceData = tool != nullptr ? tool->getData() : data;

    // What the label, the cutting instructions and the notches say.
    const VPieceLabelData &label = piece.GetPatternPieceData();
    result.insert(QStringLiteral("quantity"), label.GetQuantity());
    result.insert(QStringLiteral("onFold"), label.IsOnFold());
    result.insert(QStringLiteral("inLayout"), piece.isInLayout());
    if (!label.GetLetter().isEmpty())
    {
        result.insert(QStringLiteral("letter"), label.GetLetter());
    }
    if (!label.GetAnnotation().isEmpty())
    {
        result.insert(QStringLiteral("annotation"), label.GetAnnotation());
    }
    const VGrainlineData &grainline = piece.GetGrainlineGeometry();
    result.insert(QStringLiteral("grainline"),
                  grainline.IsVisible()
                      ? QJsonObject{{QStringLiteral("visible"), true}, {QStringLiteral("angle"), grainline.getRotation()},
                                    {QStringLiteral("length"), grainline.getLength()}}
                      : QJsonObject{{QStringLiteral("visible"), false}});
    QJsonArray notches;
    for (int i = 0; i < piece.GetPath().nodeCount(); ++i)
    {
        const VPieceNode node = piece.GetPath().at(i);
        if (node.GetTypeTool() != Tool::NodePoint || !node.isNotch())
        {
            continue;
        }
        try
        {
            const QSharedPointer<VPointF> p = pieceData->GeometricObject<VPointF>(node.GetId());
            notches.append(QJsonObject{{QStringLiteral("pointId"), static_cast<qint64>(p->getIdObject())},
                                       {QStringLiteral("name"), p->name()},
                                       {QStringLiteral("type"), kNotchTypes.value(static_cast<int>(node.getNotchType()))},
                                       {QStringLiteral("count"), node.getNotchCount()},
                                       {QStringLiteral("onCutline"), node.showNotch()},
                                       {QStringLiteral("onSeamline"), node.showSeamlineNotch()}});
        }
        catch (const VException &)
        {
            // a node that cannot be resolved is left out
        }
    }
    result.insert(QStringLiteral("notches"), notches);

    try
    {
        const QVector<QPointF> outline = piece.mainPathPoints(pieceData);
        const double unitFactor = qApp->fromPixel(1.0);
        result.insert(QStringLiteral("area"), round3(polygonArea(outline) * unitFactor * unitFactor));
        result.insert(QStringLiteral("perimeter"), round3(polygonPerimeter(outline) * unitFactor));
    }
    catch (const VException &e)
    {
        result.insert(QStringLiteral("measureError"), e.ErrorMessage());
    }
    return result;
}
} // namespace

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::addPiece(const QJsonObject &arguments)
{
    const Context ctx = currentContext();
    MainWindow *window = qobject_cast<MainWindow *>(qApp->getMainWindow());
    if (!ctx.valid())
    {
        return McpToolResult::error(ctx.error);
    }
    if (window == nullptr || window->mcpPieceScene() == nullptr)
    {
        return McpToolResult::error(QStringLiteral("The Seamly2D main window is not available."));
    }

    const QString name = stringArg(arguments, "name");
    if (name.isEmpty())
    {
        return McpToolResult::error(QStringLiteral("'name' is required (e.g. \"Front\")."));
    }
    const QHash<quint32, VPiece> *pieces = ctx.data->DataPieces();
    for (auto it = pieces->constBegin(); it != pieces->constEnd(); ++it)
    {
        if (it.value().GetName() == name)
        {
            return McpToolResult::error(QStringLiteral("A piece named \"%1\" already exists.").arg(name));
        }
    }

    const QJsonValue nodesValue = arguments.value(QStringLiteral("nodes"));
    if (!nodesValue.isArray() || nodesValue.toArray().size() < 3)
    {
        return McpToolResult::error(QStringLiteral("'nodes' must be an array with at least 3 entries: the ids of the "
                                                   "points (and curves/arcs between them) along the outline, in "
                                                   "order, e.g. [1,2,5,3]. Each entry is an id or {\"id\":..., "
                                                   "\"reverse\":true|false}."));
    }

    // --- validate the outline before touching the pattern ---------------------------------------------------------
    QVector<VPieceNode> nodes;
    QSet<quint32> used;
    const QJsonArray entries = nodesValue.toArray();
    int pointNodes = 0;
    for (int i = 0; i < entries.size(); ++i)
    {
        const QJsonValue entry = entries.at(i);
        const QJsonValue idValue = entry.isObject() ? entry.toObject().value(QStringLiteral("id")) : entry;
        if (!idValue.isDouble() || idValue.toDouble() < 1 || idValue.toDouble() != qFloor(idValue.toDouble()))
        {
            return McpToolResult::error(QStringLiteral("nodes[%1] must be an object id (see list_objects).").arg(i));
        }
        const quint32 id = static_cast<quint32>(idValue.toDouble());
        if (used.contains(id))
        {
            return McpToolResult::error(QStringLiteral("Object %1 appears twice in 'nodes'.").arg(id));
        }
        used.insert(id);

        Tool type = Tool::LAST_ONE_DO_NOT_USE;
        try
        {
            const QSharedPointer<VGObject> object = ctx.data->GetGObject(id);
            type = object.isNull() ? Tool::LAST_ONE_DO_NOT_USE : nodeTypeFor(object->getType());
        }
        catch (const VException &)
        {
            return McpToolResult::error(QStringLiteral("nodes[%1] = %2 does not exist in the active draft block (see "
                                                       "list_objects).").arg(i).arg(id));
        }
        if (type == Tool::LAST_ONE_DO_NOT_USE)
        {
            return McpToolResult::error(QStringLiteral("nodes[%1] = %2 cannot be part of an outline (use points, "
                                                       "curves and arcs; straight edges between consecutive points "
                                                       "need no entry).").arg(i).arg(id));
        }
        if (type == Tool::NodePoint)
        {
            ++pointNodes;
        }
        nodes.append(VPieceNode(id, type, entry.isObject() && entry.toObject().value(QStringLiteral("reverse"))
                                                                .toBool(false)));
    }
    if (pointNodes < 2)
    {
        return McpToolResult::error(QStringLiteral("The outline needs at least 2 points."));
    }

    // Curves and arcs are drawn from one end to the other: flip them when they run against the outline direction.
    for (int i = 0; i < nodes.size(); ++i)
    {
        if (nodes.at(i).GetTypeTool() == Tool::NodePoint
            || entries.at(i).isObject() && entries.at(i).toObject().contains(QStringLiteral("reverse")))
        {
            continue;
        }
        const VPieceNode &previous = nodes.at((i + nodes.size() - 1) % nodes.size());
        if (previous.GetTypeTool() != Tool::NodePoint)
        {
            continue;
        }
        const QSharedPointer<VAbstractCurve> curve = ctx.data->GeometricObject<VAbstractCurve>(nodes.at(i).GetId());
        const QPointF from = static_cast<QPointF>(*ctx.data->GeometricObject<VPointF>(previous.GetId()));
        nodes[i].SetReverse(QLineF(from, curve->getLastPoint()).length() < QLineF(from, curve->getFirstPoint()).length());
    }

    // seam allowance
    bool seamAllowance = false;
    if (arguments.contains(QStringLiteral("seam_allowance")))
    {
        seamAllowance = arguments.value(QStringLiteral("seam_allowance")).toBool(false);
    }
    QString width = stringArg(arguments, "seam_allowance_width");
    double widthValue = 0;
    if (arguments.value(QStringLiteral("seam_allowance_width")).isDouble())
    {
        width = QString::number(arguments.value(QStringLiteral("seam_allowance_width")).toDouble());
    }
    if (width.isEmpty())
    {
        // Same default as the Piece tool dialog: the default seam allowance of the preferences, in the pattern unit.
        widthValue = UnitConvertor(qApp->Settings()->GetDefaultSeamAllowance(), StrToUnits(qApp->Settings()->getUnit()),
                                   qApp->patternUnit());
        width = QString::number(widthValue);
    }
    else
    {
        QString error;
        if (!checkFormula(*ctx.data->DataVariables(), QStringLiteral("seam_allowance_width"), width, widthValue,
                          error))
        {
            return McpToolResult::error(error);
        }
        if (widthValue < 0)
        {
            return McpToolResult::error(QStringLiteral("'seam_allowance_width' must not be negative."));
        }
    }

    VPiece piece;
    piece.GetPath().setNodes(nodes);
    piece.SetName(name);
    piece.SetSeamAllowance(seamAllowance);
    piece.setColor(ColorBlack);
    piece.setFill(FillNone);
    piece.setInLayout(true);
    piece.setSeamAllowanceWidthFormula(width, widthValue);

    // The outline must enclose an area; reject collinear or degenerate input before anything is created.
    const QVector<QPointF> outline = piece.mainPathPoints(ctx.data);
    if (outline.size() < 3 || polygonArea(outline) < 1e-6)
    {
        return McpToolResult::error(QStringLiteral("The outline does not enclose an area (points on one line or in "
                                                   "the wrong order?). Check the node order with list_objects."));
    }

    // --- create the piece like the Piece tool dialog does ---------------------------------------------------------
    QUndoStack *stack = qApp->getUndoStack();
    stack->beginMacro(QObject::tr("Claude: add piece %1").arg(name));   // PatternPieceTool ends this macro itself
    PatternPieceTool *tool = nullptr;
    try
    {
        piece.GetPath().setNodes(ToolAccess::PrepareNodes(piece.GetPath(), window->mcpPieceScene(), ctx.doc, ctx.data));
        tool = PatternPieceTool::Create(0, piece, width, window->mcpPieceScene(), ctx.doc, ctx.data,
                                        Document::FullParse, Source::FromGui);
        if (tool != nullptr)
        {
            tool->RefreshGeometry();
        }
    }
    catch (const VException &e)
    {
        stack->endMacro();
        return McpToolResult::error(QStringLiteral("Could not create the piece: %1").arg(e.ErrorMessage()));
    }
    if (tool == nullptr)
    {
        stack->endMacro();
        return McpToolResult::error(QStringLiteral("The piece could not be created."));
    }
    window->mcpPiecesChanged();

    const quint32 id = tool->getId();
    const QJsonObject result = pieceJson(ctx.data, id, ctx.data->GetPiece(id));
    return McpToolResult::ok(QStringLiteral("Added piece \"%1\" (id %2): area %3 %4^2, perimeter %5 %4. Use show_mode "
                                            "\"piece\" to see it. Undo removes it.")
                                 .arg(name).arg(id).arg(result.value(QStringLiteral("area")).toDouble())
                                 .arg(UnitsToStr(qApp->patternUnit()))
                                 .arg(result.value(QStringLiteral("perimeter")).toDouble()),
                             result);
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::listPieces(const QJsonObject &arguments)
{
    Q_UNUSED(arguments)
    VContainer *data = qApp->getCurrentData();
    if (data == nullptr)
    {
        return McpToolResult::error(QStringLiteral("No pattern is open in Seamly2D."));
    }
    QJsonArray list;
    const QHash<quint32, VPiece> *pieces = data->DataPieces();
    QList<quint32> ids = pieces->keys();
    std::sort(ids.begin(), ids.end());
    for (quint32 id : ids)
    {
        list.append(pieceJson(data, id, pieces->value(id)));
    }
    return McpToolResult::ok(QStringLiteral("%1 piece(s) (unit: %2).").arg(list.size())
                                 .arg(UnitsToStr(qApp->patternUnit())),
                             QJsonObject{{QStringLiteral("unit"), UnitsToStr(qApp->patternUnit())},
                                         {QStringLiteral("pieces"), list}});
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::deletePiece(const QJsonObject &arguments)
{
    VContainer *data = qApp->getCurrentData();
    if (data == nullptr)
    {
        return McpToolResult::error(QStringLiteral("No pattern is open in Seamly2D."));
    }
    const QJsonValue idValue = arguments.value(QStringLiteral("id"));
    if (!idValue.isDouble() || idValue.toDouble() < 1 || idValue.toDouble() != qFloor(idValue.toDouble()))
    {
        return McpToolResult::error(QStringLiteral("'id' is required: the id of the piece (see list_pieces)."));
    }
    const quint32 id = static_cast<quint32>(idValue.toDouble());
    if (!data->DataPieces()->contains(id))
    {
        return McpToolResult::error(QStringLiteral("There is no piece with id %1 (see list_pieces).").arg(id));
    }
    const QString name = data->GetPiece(id).GetName();
    PatternPieceTool *tool = qobject_cast<PatternPieceTool *>(VAbstractPattern::getTool(id));
    if (tool == nullptr)
    {
        return McpToolResult::error(QStringLiteral("The piece %1 cannot be deleted with this tool.").arg(id));
    }
    QUndoStack *stack = qApp->getUndoStack();
    stack->beginMacro(QObject::tr("Claude: delete piece %1").arg(name));
    tool->Remove(false);   // no confirmation; throws internally and is handled inside Remove()
    // Remove(false) only schedules the re-parse when it asks for confirmation; without it the piece would stay in the
    // program's data until the next full parse. Trigger it like the confirmed path does.
    VAbstractPattern *doc = qApp->getCurrentDocument();
    emit doc->NeedFullParsing();
    stack->endMacro();
    // The same applies to Undo/Redo of that command: connect its re-parse request, which the tool left unconnected.
    if (stack->index() > 0)
    {
        const QUndoCommand *macro = stack->command(stack->index() - 1);
        for (int i = 0; macro != nullptr && i < macro->childCount(); ++i)
        {
            DeletePiece *del = const_cast<DeletePiece *>(dynamic_cast<const DeletePiece *>(macro->child(i)));
            if (del != nullptr)
            {
                QObject::connect(del, &DeletePiece::NeedFullParsing, doc, &VAbstractPattern::NeedFullParsing);
            }
        }
    }
    return McpToolResult::ok(QStringLiteral("Deleted piece \"%1\". Undo restores it.").arg(name),
                             QJsonObject{{QStringLiteral("id"), static_cast<qint64>(id)}});
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::showMode(const QJsonObject &arguments)
{
    MainWindow *window = qobject_cast<MainWindow *>(qApp->getMainWindow());
    if (window == nullptr)
    {
        return McpToolResult::error(QStringLiteral("The Seamly2D main window is not available."));
    }
    QString error;
    const QString mode = stringArg(arguments, "mode");
    if (!window->mcpShowMode(mode, error))
    {
        return McpToolResult::error(error);
    }
    return McpToolResult::ok(QStringLiteral("The window now shows the %1 view.").arg(mode));
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::savePattern(const QJsonObject &arguments)
{
    MainWindow *window = qobject_cast<MainWindow *>(qApp->getMainWindow());
    if (window == nullptr)
    {
        return McpToolResult::error(QStringLiteral("The Seamly2D main window is not available."));
    }
    QString error;
    const QJsonObject result = window->mcpSavePattern(stringArg(arguments, "path"), error);
    if (!error.isEmpty())
    {
        return McpToolResult::error(error);
    }
    return McpToolResult::ok(QStringLiteral("Saved the pattern to %1.").arg(result.value(QStringLiteral("path"))
                                                                                .toString()),
                             result);
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::openPattern(const QJsonObject &arguments)
{
    MainWindow *window = qobject_cast<MainWindow *>(qApp->getMainWindow());
    if (window == nullptr)
    {
        return McpToolResult::error(QStringLiteral("The Seamly2D main window is not available."));
    }
    QString error;
    const QJsonObject result = window->mcpOpenPattern(stringArg(arguments, "path"),
                                                      stringArg(arguments, "measurements_path"), error);
    if (!error.isEmpty())
    {
        return McpToolResult::error(error);
    }
    return McpToolResult::ok(QStringLiteral("Opened the pattern:"), result);
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::exportLayout(const QJsonObject &arguments)
{
    MainWindow *window = qobject_cast<MainWindow *>(qApp->getMainWindow());
    if (window == nullptr)
    {
        return McpToolResult::error(QStringLiteral("The Seamly2D main window is not available."));
    }
    QString error;
    const QJsonObject result = window->mcpExportLayout(arguments, error);
    if (!error.isEmpty())
    {
        return McpToolResult::error(error);
    }
    return McpToolResult::ok(QStringLiteral("Exported %1 file(s) to %2.")
                                 .arg(result.value(QStringLiteral("files")).toArray().size())
                                 .arg(result.value(QStringLiteral("directory")).toString()),
                             result);
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::screenshot(const QJsonObject &arguments)
{
    QGraphicsScene *scene = qApp->getCurrentScene();
    VMainGraphicsView *view = qApp->getSceneView();
    if (scene == nullptr || view == nullptr)
    {
        return McpToolResult::error(QStringLiteral("No pattern is open in Seamly2D."));
    }
    const QString area = stringArg(arguments, "area").isEmpty() ? QStringLiteral("drawing") : stringArg(arguments, "area");
    if (area != QLatin1String("drawing") && area != QLatin1String("window"))
    {
        return McpToolResult::error(QStringLiteral("'area' must be \"drawing\" (everything, independent of the zoom) "
                                                   "or \"window\" (what the user currently sees)."));
    }
    const int maxSize = qBound(200, arguments.contains(QStringLiteral("max_size"))
                                        ? arguments.value(QStringLiteral("max_size")).toInt(1000) : 1000, 2000);

    QImage image;
    if (area == QLatin1String("window"))
    {
        image = view->viewport()->grab().toImage();
        if (qMax(image.width(), image.height()) > maxSize)
        {
            image = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    else
    {
        // Render the scene itself so that the result does not depend on the window size or zoom.
        QRectF source = scene->itemsBoundingRect();
        if (source.isEmpty() || !source.isValid())
        {
            return McpToolResult::error(QStringLiteral("There is nothing to show yet."));
        }
        const double margin = qMax(source.width(), source.height()) * 0.04;
        source.adjust(-margin, -margin, margin, margin);
        const double scale = maxSize / qMax(source.width(), source.height());
        const QSize size(qMax(1, qRound(source.width() * scale)), qMax(1, qRound(source.height() * scale)));
        image = QImage(size, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        scene->render(&painter, QRectF(QPointF(0, 0), QSizeF(size)), source, Qt::KeepAspectRatio);
        painter.end();
    }

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
    {
        return McpToolResult::error(QStringLiteral("The image could not be created."));
    }
    McpToolResult result = McpToolResult::ok(QStringLiteral("Screenshot (%1, %2 x %3 px). The y axis points down; "
                                                            "x grows to the right.")
                                                 .arg(area).arg(image.width()).arg(image.height()));
    result.imagePng = png;
    return result;
}

namespace
{
/// A piece together with the data container that can resolve its nodes (a piece may belong to another draft block than
/// the active one, so the container of its own tool is used, like the layout code does).
struct PieceRef
{
    quint32           id = NULL_ID;
    PatternPieceTool *tool = nullptr;
    const VContainer *data = nullptr;
    VPiece            piece;
};

bool findPiece(const QJsonObject &arguments, PieceRef &ref, QString &error)
{
    VContainer *current = qApp->getCurrentData();
    if (current == nullptr || qApp->getCurrentDocument() == nullptr)
    {
        error = QStringLiteral("No pattern is open in Seamly2D.");
        return false;
    }
    const QJsonValue idValue = arguments.value(QStringLiteral("piece_id"));
    if (!idValue.isDouble() || idValue.toDouble() < 1 || idValue.toDouble() != qFloor(idValue.toDouble()))
    {
        error = QStringLiteral("'piece_id' is required: the id of a piece (see list_pieces).");
        return false;
    }
    ref.id = static_cast<quint32>(idValue.toDouble());
    if (!current->DataPieces()->contains(ref.id))
    {
        error = QStringLiteral("There is no piece with id %1 (see list_pieces).").arg(ref.id);
        return false;
    }
    ref.tool = qobject_cast<PatternPieceTool *>(VAbstractPattern::getTool(ref.id));
    if (ref.tool == nullptr)
    {
        error = QStringLiteral("The piece %1 cannot be changed with this tool.").arg(ref.id);
        return false;
    }
    ref.data = ref.tool->getData();
    ref.piece = ref.data->GetPiece(ref.id);
    if (ref.piece.isLocked())
    {
        error = QStringLiteral("The piece \"%1\" is locked; unlock it in Seamly2D first.").arg(ref.piece.GetName());
        return false;
    }
    return true;
}

/// Pushes the change like the piece dialog does (SavePieceOptions) and reports the new state of the piece.
McpToolResult applyPieceChange(const PieceRef &ref, const VPiece &newPiece, const QString &undoText,
                               const QString &message)
{
    VAbstractPattern *doc = qApp->getCurrentDocument();
    QUndoStack *stack = qApp->getUndoStack();
    stack->beginMacro(undoText);
    SavePieceOptions *command = new SavePieceOptions(ref.piece, newPiece, doc, ref.id);
    QObject::connect(command, &SavePieceOptions::NeedLiteParsing, doc, &VAbstractPattern::LiteParseTree);
    stack->push(command);
    stack->endMacro();
    // Re-read: the lite parse rebuilds the piece data.
    const VContainer *data = ref.tool != nullptr ? ref.tool->getData() : qApp->getCurrentData();
    return McpToolResult::ok(message, pieceJson(data, ref.id, data->GetPiece(ref.id)));
}

const QStringList kNotchSubTypes{QStringLiteral("straightforward"), QStringLiteral("bisector"),
                                 QStringLiteral("intersection")};
const QStringList kArrowTypes{QStringLiteral("both"), QStringLiteral("top"), QStringLiteral("bottom")};
} // namespace

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::updatePiece(const QJsonObject &arguments)
{
    PieceRef ref;
    QString error;
    if (!findPiece(arguments, ref, error))
    {
        return McpToolResult::error(error);
    }
    static const QStringList known{QStringLiteral("piece_id"), QStringLiteral("name"), QStringLiteral("quantity"),
                                   QStringLiteral("letter"), QStringLiteral("annotation"), QStringLiteral("on_fold"),
                                   QStringLiteral("in_layout"), QStringLiteral("seam_allowance"),
                                   QStringLiteral("seam_allowance_width")};
    for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it)
    {
        if (!known.contains(it.key()))
        {
            return McpToolResult::error(QStringLiteral("'%1' cannot be changed here. Changeable: %2.")
                                            .arg(it.key(), known.mid(1).join(QStringLiteral(", "))));
        }
    }
    if (arguments.size() < 2)
    {
        return McpToolResult::error(QStringLiteral("Nothing to change: give at least one of: %1.")
                                        .arg(known.mid(1).join(QStringLiteral(", "))));
    }

    VPiece piece = ref.piece;
    if (arguments.contains(QStringLiteral("name")))
    {
        const QString name = stringArg(arguments, "name");
        if (name.isEmpty())
        {
            return McpToolResult::error(QStringLiteral("'name' must not be empty."));
        }
        const QHash<quint32, VPiece> *pieces = qApp->getCurrentData()->DataPieces();
        for (auto it = pieces->constBegin(); it != pieces->constEnd(); ++it)
        {
            if (it.key() != ref.id && it.value().GetName() == name)
            {
                return McpToolResult::error(QStringLiteral("A piece named \"%1\" already exists.").arg(name));
            }
        }
        piece.SetName(name);
    }
    if (arguments.contains(QStringLiteral("quantity")))
    {
        const QJsonValue quantity = arguments.value(QStringLiteral("quantity"));
        if (!quantity.isDouble() || quantity.toDouble() != qFloor(quantity.toDouble()) || quantity.toDouble() < 1
            || quantity.toDouble() > 999)
        {
            return McpToolResult::error(QStringLiteral("'quantity' must be a whole number from 1 to 999: how many "
                                                       "pieces to cut."));
        }
        piece.GetPatternPieceData().SetQuantity(static_cast<int>(quantity.toDouble()));
    }
    if (arguments.contains(QStringLiteral("letter")))
    {
        piece.GetPatternPieceData().SetLetter(arguments.value(QStringLiteral("letter")).toString());
    }
    if (arguments.contains(QStringLiteral("annotation")))
    {
        piece.GetPatternPieceData().SetAnnotation(arguments.value(QStringLiteral("annotation")).toString());
    }
    if (arguments.contains(QStringLiteral("on_fold")))
    {
        piece.GetPatternPieceData().SetOnFold(arguments.value(QStringLiteral("on_fold")).toBool(false));
    }
    if (arguments.contains(QStringLiteral("in_layout")))
    {
        piece.setInLayout(arguments.value(QStringLiteral("in_layout")).toBool(true));
    }
    if (arguments.contains(QStringLiteral("seam_allowance")) || arguments.contains(QStringLiteral("seam_allowance_width")))
    {
        const bool seamAllowance = arguments.contains(QStringLiteral("seam_allowance"))
                                       ? arguments.value(QStringLiteral("seam_allowance")).toBool(false)
                                       : piece.hasSeamAllowance();
        QString width = arguments.value(QStringLiteral("seam_allowance_width")).isDouble()
                            ? QString::number(arguments.value(QStringLiteral("seam_allowance_width")).toDouble())
                            : stringArg(arguments, "seam_allowance_width");
        double value = piece.GetSAWidth();
        if (width.isEmpty())
        {
            width = piece.getSeamAllowanceWidthFormula();
        }
        else if (!checkFormula(*ref.data->DataVariables(), QStringLiteral("seam_allowance_width"), width, value, error))
        {
            return McpToolResult::error(error);
        }
        if (value < 0)
        {
            return McpToolResult::error(QStringLiteral("'seam_allowance_width' must not be negative."));
        }
        piece.SetSeamAllowance(seamAllowance);
        piece.setSeamAllowanceWidthFormula(width, value);
    }
    return applyPieceChange(ref, piece, QObject::tr("Claude: change piece %1").arg(ref.piece.GetName()),
                            QStringLiteral("Changed piece \"%1\". Undo restores the old values.")
                                .arg(piece.GetName()));
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::setGrainline(const QJsonObject &arguments)
{
    PieceRef ref;
    QString error;
    if (!findPiece(arguments, ref, error))
    {
        return McpToolResult::error(error);
    }
    VPiece piece = ref.piece;
    VGrainlineData &grainline = piece.GetGrainlineGeometry();

    if (arguments.contains(QStringLiteral("visible")) && !arguments.value(QStringLiteral("visible")).toBool(true))
    {
        grainline.SetVisible(false);
        return applyPieceChange(ref, piece, QObject::tr("Claude: hide grainline of %1").arg(piece.GetName()),
                                QStringLiteral("Hid the grainline of piece \"%1\".").arg(piece.GetName()));
    }

    // Defaults: vertical, 60 % of the piece height, an arrow at both ends.
    const QVector<QPointF> outline = piece.mainPathPoints(ref.data);
    const QRectF box = QPolygonF(outline).boundingRect();
    QString angle = arguments.value(QStringLiteral("angle")).isDouble()
                        ? QString::number(arguments.value(QStringLiteral("angle")).toDouble()) : stringArg(arguments, "angle");
    QString length = arguments.value(QStringLiteral("length")).isDouble()
                         ? QString::number(arguments.value(QStringLiteral("length")).toDouble()) : stringArg(arguments, "length");
    QString arrowLength = arguments.value(QStringLiteral("arrow_length")).isDouble()
                              ? QString::number(arguments.value(QStringLiteral("arrow_length")).toDouble())
                              : stringArg(arguments, "arrow_length");
    if (angle.isEmpty())
    {
        angle = QStringLiteral("90");
    }
    if (length.isEmpty())
    {
        length = QString::number(qRound(qApp->fromPixel(box.height()) * 0.6 * 10) / 10.0);
    }
    if (arrowLength.isEmpty())
    {
        arrowLength = QString::number(UnitConvertor(5.0, Unit::Mm, qApp->patternUnit()));
    }
    const QString arrows = stringArg(arguments, "arrows").isEmpty() ? QStringLiteral("both")
                                                                      : stringArg(arguments, "arrows").toLower();
    if (!kArrowTypes.contains(arrows))
    {
        return McpToolResult::error(QStringLiteral("'arrows' must be one of: %1.").arg(kArrowTypes.join(QStringLiteral(", "))));
    }

    double value = 0;
    const QHash<QString, QSharedPointer<VInternalVariable>> variables = *ref.data->DataVariables();
    if (!checkFormula(variables, QStringLiteral("angle"), angle, value, error)
        || !checkFormula(variables, QStringLiteral("length"), length, value, error))
    {
        return McpToolResult::error(error);
    }
    if (value <= 0)
    {
        return McpToolResult::error(QStringLiteral("'length' must be greater than 0."));
    }
    const double lengthValue = value;
    if (!checkFormula(variables, QStringLiteral("arrow_length"), arrowLength, value, error))
    {
        return McpToolResult::error(error);
    }

    grainline.SetVisible(true);
    grainline.setRotation(angle);
    grainline.setLength(length);
    grainline.setArrowLength(arrowLength);
    grainline.setArrowType(static_cast<ArrowType>(kArrowTypes.indexOf(arrows)));
    // A grainline without anchor points sits at a position; put it in the middle of the piece, like the piece dialog.
    grainline.setCenterAnchorPoint(NULL_ID);
    grainline.setTopAnchorPoint(NULL_ID);
    grainline.setBottomAnchorPoint(NULL_ID);
    grainline.SetPos(QPointF(box.center().x(), box.center().y() + qApp->toPixel(lengthValue) / 2.0));

    return applyPieceChange(ref, piece, QObject::tr("Claude: set grainline of %1").arg(piece.GetName()),
                            QStringLiteral("Set the grainline of piece \"%1\" (angle %2, length %3). Undo removes it.")
                                .arg(piece.GetName(), angle, length));
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::setNotch(const QJsonObject &arguments)
{
    PieceRef ref;
    QString error;
    if (!findPiece(arguments, ref, error))
    {
        return McpToolResult::error(error);
    }
    const QJsonValue pointValue = arguments.value(QStringLiteral("point_id"));
    if (!pointValue.isDouble() || pointValue.toDouble() < 1)
    {
        return McpToolResult::error(QStringLiteral("'point_id' is required: the id of a point of the piece's outline "
                                                   "(the id used in add_piece)."));
    }
    const quint32 pointId = static_cast<quint32>(pointValue.toDouble());

    // The piece's nodes are copies of the drawing's points; find the one that belongs to point_id.
    VPiece piece = ref.piece;
    int index = -1;
    for (int i = 0; i < piece.GetPath().nodeCount() && index < 0; ++i)
    {
        const VPieceNode node = piece.GetPath().at(i);
        if (node.GetTypeTool() != Tool::NodePoint)
        {
            continue;
        }
        try
        {
            if (ref.data->GeometricObject<VPointF>(node.GetId())->getIdObject() == pointId)
            {
                index = i;
            }
        }
        catch (const VException &)
        {
            // not a point of this container
        }
    }
    if (index < 0)
    {
        return McpToolResult::error(QStringLiteral("Point %1 is not a point of the outline of piece \"%2\".")
                                        .arg(pointId).arg(piece.GetName()));
    }
    VPieceNode node = piece.GetPath().at(index);

    const bool remove = arguments.contains(QStringLiteral("notch")) && !arguments.value(QStringLiteral("notch")).toBool(true);
    if (remove)
    {
        node.setNotch(false);
        node.setShowNotch(true);
        node.setShowSeamlineNotch(true);
        piece.GetPath()[index] = node;
        return applyPieceChange(ref, piece, QObject::tr("Claude: remove notch from %1").arg(piece.GetName()),
                                QStringLiteral("Removed the notch at point %1 of piece \"%2\".").arg(pointId).arg(piece.GetName()));
    }

    // Defaults come from the program's settings (given in the program's unit).
    VSettings *settings = qApp->Seamly2DSettings();
    const Unit settingsUnit = StrToUnits(settings->getUnit());
    NotchType type = stringToNotchType(settings->getDefaultNotchType());
    double length = UnitConvertor(settings->getDefaultNotchLength(), settingsUnit, qApp->patternUnit());
    double width = UnitConvertor(settings->getDefaultNotchWidth(), settingsUnit, qApp->patternUnit());
    NotchSubType subType = node.isNotch() ? node.getNotchSubType() : NotchSubType::Straightforward;
    int count = node.isNotch() ? node.getNotchCount() : 1;
    // A new notch is shown on the seam line, and on the cut line when the piece has a seam allowance: the program's
    // settings can have both switched off, which would make the notch invisible.
    bool onCutline = node.isNotch() ? node.showNotch() : piece.hasSeamAllowance();
    bool onSeamline = node.isNotch() ? node.showSeamlineNotch() : true;
    if (node.isNotch())
    {
        type = node.getNotchType();
        length = node.getNotchLength();
        width = node.getNotchWidth();
    }

    if (arguments.contains(QStringLiteral("type")))
    {
        const int i = kNotchTypes.indexOf(stringArg(arguments, "type").toLower());
        if (i < 0)
        {
            return McpToolResult::error(QStringLiteral("'type' must be one of: %1.").arg(kNotchTypes.join(QStringLiteral(", "))));
        }
        static const NotchType types[] = {NotchType::Slit, NotchType::TNotch, NotchType::VInternal,
                                          NotchType::VExternal, NotchType::UNotch, NotchType::Castle, NotchType::Diamond};
        type = types[i];
    }
    if (arguments.contains(QStringLiteral("sub_type")))
    {
        const int i = kNotchSubTypes.indexOf(stringArg(arguments, "sub_type").toLower());
        if (i < 0)
        {
            return McpToolResult::error(QStringLiteral("'sub_type' must be one of: %1.").arg(kNotchSubTypes.join(QStringLiteral(", "))));
        }
        static const NotchSubType subTypes[] = {NotchSubType::Straightforward, NotchSubType::Bisector,
                                                NotchSubType::Intersection};
        subType = subTypes[i];
    }
    if (arguments.contains(QStringLiteral("count")))
    {
        const double c = arguments.value(QStringLiteral("count")).toDouble(-1);
        if (c != 1 && c != 2 && c != 3)
        {
            return McpToolResult::error(QStringLiteral("'count' must be 1, 2 or 3."));
        }
        count = static_cast<int>(c);
    }
    if (arguments.contains(QStringLiteral("length")))
    {
        length = arguments.value(QStringLiteral("length")).toDouble(-1);
        if (length <= 0)
        {
            return McpToolResult::error(QStringLiteral("'length' must be a number greater than 0."));
        }
    }
    if (arguments.contains(QStringLiteral("width")))
    {
        width = arguments.value(QStringLiteral("width")).toDouble(-1);
        if (width <= 0)
        {
            return McpToolResult::error(QStringLiteral("'width' must be a number greater than 0."));
        }
    }
    if (arguments.contains(QStringLiteral("show_on_cutline")))
    {
        onCutline = arguments.value(QStringLiteral("show_on_cutline")).toBool(true);
    }
    if (arguments.contains(QStringLiteral("show_on_seamline")))
    {
        onSeamline = arguments.value(QStringLiteral("show_on_seamline")).toBool(true);
    }

    node.setNotch(true);
    node.setNotchType(type);
    node.setNotchSubType(subType);
    node.setNotchCount(count);
    node.setNotchLength(length);
    node.setNotchWidth(width);
    node.setShowNotch(onCutline);
    node.setShowSeamlineNotch(onSeamline);
    piece.GetPath()[index] = node;
    return applyPieceChange(ref, piece, QObject::tr("Claude: set notch of %1").arg(piece.GetName()),
                            QStringLiteral("Set a notch at point %1 of piece \"%2\". Undo restores the old state.")
                                .arg(pointId).arg(piece.GetName()));
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::setMeasurement(const QJsonObject &arguments)
{
    MainWindow *window = qobject_cast<MainWindow *>(qApp->getMainWindow());
    if (window == nullptr)
    {
        return McpToolResult::error(QStringLiteral("The Seamly2D main window is not available."));
    }
    const QString name = stringArg(arguments, "name");
    const QJsonValue value = arguments.value(QStringLiteral("value"));
    if (name.isEmpty() || !value.isDouble())
    {
        return McpToolResult::error(QStringLiteral("'name' (a measurement from list_variables) and 'value' (a number "
                                                   "in the pattern unit) are required."));
    }
    QString error;
    const QJsonObject result = window->mcpSetMeasurement(name, value.toDouble(), error);
    if (!error.isEmpty())
    {
        return McpToolResult::error(error);
    }
    return McpToolResult::ok(QStringLiteral("Changed measurement %1 from %2 to %3 %4 in %5. Undo writes the old "
                                            "value back.")
                                 .arg(name).arg(result.value(QStringLiteral("oldValue")).toDouble())
                                 .arg(result.value(QStringLiteral("value")).toDouble())
                                 .arg(result.value(QStringLiteral("unit")).toString(),
                                      result.value(QStringLiteral("file")).toString()),
                             result);
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::undoRedo(const QJsonObject &arguments, bool undo)
{
    MainWindow *window = qobject_cast<MainWindow *>(qApp->getMainWindow());
    if (window == nullptr)
    {
        return McpToolResult::error(QStringLiteral("The Seamly2D main window is not available."));
    }
    const QJsonValue stepsValue = arguments.value(QStringLiteral("steps"));
    const int steps = stepsValue.isDouble() ? static_cast<int>(stepsValue.toDouble()) : 1;
    if (steps < 1 || steps > 50)
    {
        return McpToolResult::error(QStringLiteral("'steps' must be from 1 to 50."));
    }
    QJsonArray done;
    for (int i = 0; i < steps; ++i)
    {
        const QString text = window->mcpUndo(undo);
        if (text.isEmpty())
        {
            break;
        }
        done.append(text);
    }
    if (done.isEmpty())
    {
        return McpToolResult::error(undo ? QStringLiteral("There is nothing to undo.")
                                         : QStringLiteral("There is nothing to redo."));
    }
    return McpToolResult::ok(QStringLiteral("%1 %2 step(s).").arg(undo ? QStringLiteral("Undid") : QStringLiteral("Redid"))
                                 .arg(done.size()),
                             QJsonObject{{QStringLiteral("steps"), done}});
}

namespace
{
/// Gives access to the table of standard measurements (a protected member of VTranslateMeasurements).
class StandardMeasurements : public VTranslateMeasurements
{
public:
    QStringList names() const { return m_measurements.keys(); }
};
} // namespace

//---------------------------------------------------------------------------------------------------------------------
QStringList PatternEditor::standardMeasurementNames()
{
    static const StandardMeasurements table;
    return table.names();
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::listStandardMeasurements(const QJsonObject &arguments)
{
    static const StandardMeasurements table;
    const QString query = stringArg(arguments, "query");
    const QJsonValue limitValue = arguments.value(QStringLiteral("limit"));
    const int limit = qBound(1, limitValue.isDouble() ? static_cast<int>(limitValue.toDouble()) : 40, 400);
    QJsonArray list;
    int matches = 0;
    for (const QString &name : table.names())
    {
        const QString label = table.guiText(name);
        const QString description = table.Description(name);
        const QString code = table.MNumber(name);
        if (!query.isEmpty() && !name.contains(query, Qt::CaseInsensitive) && !label.contains(query, Qt::CaseInsensitive)
            && !description.contains(query, Qt::CaseInsensitive) && !code.contains(query, Qt::CaseInsensitive))
        {
            continue;
        }
        ++matches;
        if (list.size() < limit)
        {
            list.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("code"), code},
                                    {QStringLiteral("label"), label}, {QStringLiteral("description"), description}});
        }
    }
    return McpToolResult::ok(QStringLiteral("%1 of %2 matching standard measurement(s). Use 'name' in create_measurements "
                                            "and in formulas; custom measurements are written @name.")
                                 .arg(list.size()).arg(matches),
                             QJsonObject{{QStringLiteral("total"), table.names().size()},
                                         {QStringLiteral("matches"), matches},
                                         {QStringLiteral("measurements"), list}});
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::createMeasurements(const QJsonObject &arguments)
{
    MainWindow *window = qobject_cast<MainWindow *>(qApp->getMainWindow());
    if (window == nullptr)
    {
        return McpToolResult::error(QStringLiteral("The Seamly2D main window is not available."));
    }
    const QJsonValue values = arguments.value(QStringLiteral("values"));
    if (!values.isObject())
    {
        return McpToolResult::error(QStringLiteral("'values' is required: an object mapping measurement names to "
                                                   "numbers, e.g. {\"waist_circ\": 84, \"hip_circ\": 100}."));
    }
    QString error;
    const QJsonObject result = window->mcpCreateMeasurements(stringArg(arguments, "path"), stringArg(arguments, "unit"),
                                                             values.toObject(), stringArg(arguments, "given_name"),
                                                             stringArg(arguments, "family_name"), error);
    if (!error.isEmpty())
    {
        return McpToolResult::error(error);
    }
    return McpToolResult::ok(QStringLiteral("Created %1 with %2 measurement(s) in %3. Use attach_measurements to use it "
                                            "in the pattern.")
                                 .arg(result.value(QStringLiteral("path")).toString())
                                 .arg(result.value(QStringLiteral("measurements")).toInt())
                                 .arg(result.value(QStringLiteral("unit")).toString()),
                             result);
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::attachMeasurements(const QJsonObject &arguments)
{
    MainWindow *window = qobject_cast<MainWindow *>(qApp->getMainWindow());
    if (window == nullptr)
    {
        return McpToolResult::error(QStringLiteral("The Seamly2D main window is not available."));
    }
    QString error;
    const QJsonObject result = window->mcpAttachMeasurements(stringArg(arguments, "path"), error);
    if (!error.isEmpty())
    {
        return McpToolResult::error(error);
    }
    return McpToolResult::ok(QStringLiteral("The pattern now uses %1 (%2 measurements, unit %3). Measurement names can "
                                            "be used in formulas; see list_variables for their values.")
                                 .arg(result.value(QStringLiteral("path")).toString())
                                 .arg(result.value(QStringLiteral("measurements")).toInt())
                                 .arg(result.value(QStringLiteral("unit")).toString()),
                             result);
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult PatternEditor::addDraftBlock(const QJsonObject &arguments)
{
    MainWindow *window = qobject_cast<MainWindow *>(qApp->getMainWindow());
    if (window == nullptr)
    {
        return McpToolResult::error(QStringLiteral("The Seamly2D main window is not available."));
    }
    QString error;
    const QJsonObject result = window->mcpAddDraftBlock(stringArg(arguments, "name"), stringArg(arguments, "unit"),
                                                        error);
    if (!error.isEmpty())
    {
        return McpToolResult::error(error);
    }
    const QJsonObject basePoint = result.value(QStringLiteral("basePoint")).toObject();
    return McpToolResult::ok(QStringLiteral("Created draft block \"%1\" (unit %2) with base point %3 (id %4). It is "
                                            "now the active draft block. Undo removes it.")
                                 .arg(result.value(QStringLiteral("draftBlock")).toString(),
                                      result.value(QStringLiteral("unit")).toString(),
                                      basePoint.value(QStringLiteral("name")).toString())
                                 .arg(basePoint.value(QStringLiteral("id")).toInt()),
                             result);
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::deleteObjectTool()
{
    McpTool tool;
    tool.name = QStringLiteral("delete_object");
    tool.description =
        QStringLiteral("Deletes one object (a point or a line) of the active draft block. One Undo step. Refused, "
                       "with the names of the dependents, if other objects still use it (delete or change those "
                       "first), and for the base point of a draft block.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("id"), idProperty(QStringLiteral("Id of the object "
                                                                                               "to delete."))}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("id")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::deleteObject(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::updateObjectTool()
{
    McpTool tool;
    tool.name = QStringLiteral("update_object");
    tool.description =
        QStringLiteral("Changes parameters of an existing object; everything that depends on it follows "
                       "automatically. One Undo step. Give the object's 'id' plus only the parameters to change; "
                       "the parameters are the same as when the object was created: name, length, angle (formulas "
                       "or numbers), the referenced point ids (base_point_id, second_point_id, first_point_id, "
                       "vertex_point_id, third_point_id, line1_point1_id, line_point1_id, axis_point1_id, curve_id, ...; 'from' "
                       "\"start\"/\"end\" for cutCurve points), and x/y for the base point of a "
                       "draft block (pattern unit). Which ones apply depends on the object: see its definition in "
                       "list_objects. Errors (bad formula, reference to a later object, a name still used in "
                       "formulas) leave the pattern unchanged.");
    QJsonObject properties;
    properties.insert(QStringLiteral("id"), idProperty(QStringLiteral("Id of the object to change.")));
    properties.insert(QStringLiteral("name"), stringProperty(QStringLiteral("New point name.")));
    properties.insert(QStringLiteral("length"), stringProperty(QStringLiteral("New number or formula.")));
    properties.insert(QStringLiteral("angle"), stringProperty(QStringLiteral("New number or formula in degrees.")));
    properties.insert(QStringLiteral("x"), stringProperty(QStringLiteral("Base point only: x in the pattern unit.")));
    properties.insert(QStringLiteral("y"), stringProperty(QStringLiteral("Base point only: y in the pattern unit.")));
    for (const char *key : {"base_point_id", "first_point_id", "second_point_id", "vertex_point_id", "third_point_id",
                            "line1_point1_id", "line1_point2_id", "line2_point1_id", "line2_point2_id",
                            "line_point1_id", "line_point2_id", "shoulder_point_id", "axis_point1_id",
                            "axis_point2_id", "x_from_point_id", "y_from_point_id", "curve_id"})
    {
        properties.insert(QLatin1String(key), idProperty(QStringLiteral("New referenced point (or, for curve_id, "
                                                                         "curve) id: an earlier object.")));
    }
    properties.insert(QStringLiteral("from"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("start"), QStringLiteral("end")}},
                                  {QStringLiteral("description"), QStringLiteral("cutCurve points: count from the "
                                                                                 "start or the end.")}});
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), properties},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("id")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::updateObject(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::listVariablesTool()
{
    McpTool tool;
    tool.name = QStringLiteral("list_variables");
    tool.description =
        QStringLiteral("Lists the variables of the pattern: the custom variables (increments) with name, formula, "
                       "value and description, and the measurements of the loaded measurement file with their "
                       "values (pattern unit). In formulas a custom variable is written as its 'reference' (#name) "
                       "and a measurement by its name. Read this before writing formulas that should depend on "
                       "sizes.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject()}};
    tool.readOnly = true;
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::listVariables(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::setVariableTool()
{
    McpTool tool;
    tool.name = QStringLiteral("set_variable");
    tool.description =
        QStringLiteral("Creates a custom variable (increment) or changes an existing one, so that sizes can be "
                       "changed in one place: formulas of points refer to it as #name and follow when it changes. "
                       "'name' is given without the #. 'formula' is a number or a formula that may use "
                       "measurements and other variables (written #othername); it is required for a new variable "
                       "and optional when changing an existing one's description. One Undo step. Invalid "
                       "formulas, names and circular references leave the pattern unchanged.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("name"),
                                                 stringProperty(QStringLiteral("Variable name without #: letters, "
                                                                               "digits, underscore. Example: "
                                                                               "\"skirt_length\"."))},
                                                {QStringLiteral("formula"),
                                                 stringProperty(QStringLiteral("Number or formula in the pattern "
                                                                               "unit. Example: \"60\" or "
                                                                               "\"#waist_ease*2\"."))},
                                                {QStringLiteral("description"),
                                                 stringProperty(QStringLiteral("Optional description."))}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("name")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::setVariable(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::deleteVariableTool()
{
    McpTool tool;
    tool.name = QStringLiteral("delete_variable");
    tool.description =
        QStringLiteral("Deletes a custom variable. One Undo step. Refused, naming the places, while a formula of "
                       "a point, line or another variable still uses it.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("name"),
                                                 stringProperty(QStringLiteral("Variable name without #."))}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("name")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::deleteVariable(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::addCurveTool()
{
    McpTool tool;
    tool.name = QStringLiteral("add_curve");
    tool.description =
        QStringLiteral("Draws a curve between existing points of the active draft block. It appears on the canvas "
                       "immediately and is one Undo step. The result contains the curve's id, name and length. "
                       "Types:\n"
                       "- spline: a smooth curve from 'start_point_id' to 'end_point_id'. 'angle1' is the "
                       "direction in degrees in which the curve leaves the start point and 'angle2' the direction "
                       "in which it arrives at the end point (0 = right, 90 = up, 180 = left, 270 = down on the "
                       "screen); 'length1' and 'length2' (pattern unit, formulas allowed) are the lengths of the "
                       "control handles at start and end: longer handles make a rounder curve.\n"
                       "- cubicBezier: a Bezier curve through 'start_point_id' and 'end_point_id' that is pulled "
                       "towards the control points 'control1_point_id' (at the start) and 'control2_point_id' (at "
                       "the end), which must be existing points.\n"
                       "- path: one smooth curve through three or more existing points in the order given "
                       "('points'): armholes, necklines, hip curves. Without further input the curve runs smoothly "
                       "through the points (the direction at each point follows its neighbours, handles are a third "
                       "of the distance to the neighbour). To shape it, an entry can be {\"point_id\":..., "
                       "\"angle\": direction of travel at that point in degrees, \"length_in\" / \"length_out\": "
                       "handle lengths before / after the point (pattern unit, formulas allowed)}; a longer handle "
                       "makes the curve fuller there. The ends use only the handle that points into the curve.\n"
                       "Errors leave the pattern unchanged.");
    QJsonObject properties;
    properties.insert(QStringLiteral("type"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("spline"),
                                                                      VToolCubicBezier::ToolType,
                                                                      QStringLiteral("path")}},
                                  {QStringLiteral("description"), QStringLiteral("Kind of curve.")}});
    properties.insert(QStringLiteral("start_point_id"), idProperty(QStringLiteral("Id of the start point.")));
    properties.insert(QStringLiteral("end_point_id"), idProperty(QStringLiteral("Id of the end point.")));
    properties.insert(QStringLiteral("control1_point_id"),
                      idProperty(QStringLiteral("cubicBezier: id of the first control point.")));
    properties.insert(QStringLiteral("control2_point_id"),
                      idProperty(QStringLiteral("cubicBezier: id of the second control point.")));
    properties.insert(QStringLiteral("points"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                  {QStringLiteral("minItems"), 3},
                                  {QStringLiteral("items"),
                                   QJsonObject{{QStringLiteral("oneOf"),
                                                QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}},
                                                           QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}}}},
                                  {QStringLiteral("description"),
                                   QStringLiteral("path: the point ids in the order the curve runs; an entry may be "
                                                  "an object with point_id, angle, length_in, length_out.")}});
    properties.insert(QStringLiteral("angle1"),
                      stringProperty(QStringLiteral("spline: leaving direction at the start, degrees.")));
    properties.insert(QStringLiteral("angle2"),
                      stringProperty(QStringLiteral("spline: arriving direction at the end, degrees.")));
    properties.insert(QStringLiteral("length1"),
                      stringProperty(QStringLiteral("spline: handle length at the start (number or formula).")));
    properties.insert(QStringLiteral("length2"),
                      stringProperty(QStringLiteral("spline: handle length at the end (number or formula).")));
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), properties},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("type")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::addCurve(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::addArcTool()
{
    McpTool tool;
    tool.name = QStringLiteral("add_arc");
    tool.description =
        QStringLiteral("Draws a circular arc around an existing point. It appears on the canvas immediately and is "
                       "one Undo step. The arc runs counter-clockwise (as seen on the screen) from 'start_angle' to "
                       "'end_angle' (degrees, 0 = right, 90 = up) at distance 'radius' (pattern unit, formula "
                       "allowed, greater than 0) from the center point. The result contains the arc's id, name and "
                       "length.");
    QJsonObject properties;
    properties.insert(QStringLiteral("center_point_id"), idProperty(QStringLiteral("Id of the center point.")));
    properties.insert(QStringLiteral("radius"), stringProperty(QStringLiteral("Radius, number or formula.")));
    properties.insert(QStringLiteral("start_angle"), stringProperty(QStringLiteral("Start angle in degrees.")));
    properties.insert(QStringLiteral("end_angle"), stringProperty(QStringLiteral("End angle in degrees.")));
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), properties},
                                   {QStringLiteral("required"),
                                    QJsonArray{QStringLiteral("center_point_id"), QStringLiteral("radius"),
                                               QStringLiteral("start_angle"), QStringLiteral("end_angle")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::addArc(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::addPieceTool()
{
    McpTool tool;
    tool.name = QStringLiteral("add_piece");
    tool.description =
        QStringLiteral("Creates a pattern piece from the outline of the active draft block: the piece is what gets "
                       "laid out, graded and exported. 'nodes' lists the ids of the outline's points in order "
                       "(clockwise is the convention), and between two points the id of a curve or arc if the edge "
                       "between them is curved; a straight edge needs no entry. The outline is closed "
                       "automatically. Curves are flipped to run along the outline direction unless you give "
                       "{\"id\":..., \"reverse\":true|false}. Optionally add a seam allowance. The result reports "
                       "the piece's area and perimeter so that you can check it. One Undo step. Refused if the "
                       "outline encloses no area, an id is unknown or repeated, or the name is taken.");
    QJsonObject nodeItem{{QStringLiteral("oneOf"),
                          QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}},
                                     QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                 {QStringLiteral("properties"),
                                                  QJsonObject{{QStringLiteral("id"),
                                                               QJsonObject{{QStringLiteral("type"),
                                                                            QStringLiteral("integer")}}},
                                                              {QStringLiteral("reverse"),
                                                               QJsonObject{{QStringLiteral("type"),
                                                                            QStringLiteral("boolean")}}}}},
                                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("id")}}}}}};
    QJsonObject properties;
    properties.insert(QStringLiteral("name"), stringProperty(QStringLiteral("Piece name, unique. Example: \"Front\".")));
    properties.insert(QStringLiteral("nodes"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                  {QStringLiteral("minItems"), 3},
                                  {QStringLiteral("items"), nodeItem},
                                  {QStringLiteral("description"),
                                   QStringLiteral("Ids along the outline: points, and curves/arcs between points.")}});
    properties.insert(QStringLiteral("seam_allowance"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                                  {QStringLiteral("description"),
                                   QStringLiteral("Add a seam allowance around the outline (default false).")}});
    properties.insert(QStringLiteral("seam_allowance_width"),
                      stringProperty(QStringLiteral("Seam allowance width, number or formula in the pattern unit; "
                                                    "defaults to the program's default seam allowance.")));
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), properties},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("name"),
                                                                           QStringLiteral("nodes")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::addPiece(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::listPiecesTool()
{
    McpTool tool;
    tool.name = QStringLiteral("list_pieces");
    tool.description = QStringLiteral("Lists the pattern pieces of the whole pattern with id, name, number of outline "
                                      "nodes, seam allowance, area and perimeter (pattern unit).");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject()}};
    tool.readOnly = true;
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::listPieces(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::deletePieceTool()
{
    McpTool tool;
    tool.name = QStringLiteral("delete_piece");
    tool.description = QStringLiteral("Deletes a pattern piece (not the drawing it was made from). One Undo step.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("id"),
                                                 idProperty(QStringLiteral("Piece id from list_pieces."))}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("id")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::deletePiece(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::showModeTool()
{
    McpTool tool;
    tool.name = QStringLiteral("show_mode");
    tool.description = QStringLiteral("Switches what the Seamly2D window shows: \"draft\" (the construction drawing) "
                                      "or \"piece\" (the pattern pieces; needs at least one piece). Does not change "
                                      "the pattern.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("mode"),
                                                 QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                             {QStringLiteral("enum"),
                                                              QJsonArray{QStringLiteral("draft"),
                                                                         QStringLiteral("piece")}}}}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("mode")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::showMode(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::savePatternTool()
{
    McpTool tool;
    tool.name = QStringLiteral("save_pattern");
    tool.description =
        QStringLiteral("Saves the pattern to a .sm2d file. Without 'path' it saves to the file the pattern was opened "
                       "from or saved to before. The first time in a session that a file is written, Seamly2D asks "
                       "the user to confirm (and shows the path), so expect a short delay; if the user declines, the "
                       "call fails and nothing is written. 'path' must be absolute; \".sm2d\" is appended if the "
                       "name has no extension.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("path"),
                                                 stringProperty(QStringLiteral("Absolute path of the file to "
                                                                               "write. Optional when the pattern "
                                                                               "already has a file."))}}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::savePattern(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::openPatternTool()
{
    McpTool tool;
    tool.name = QStringLiteral("open_pattern");
    tool.description =
        QStringLiteral("Opens an existing pattern file (.sm2d) into this window. Only works while the window is "
                       "empty (no pattern, no unsaved changes): otherwise Seamly2D would open another window that "
                       "this connection cannot control, and the call fails with an explanation. Seamly2D asks the "
                       "user to confirm the file first. Optionally give the absolute path of a measurement file "
                       "(.smis/.smms) to use instead of the one the pattern refers to. 'path' must be absolute.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("path"),
                                                 stringProperty(QStringLiteral("Absolute path of the .sm2d file."))},
                                                {QStringLiteral("measurements_path"),
                                                 stringProperty(QStringLiteral("Optional absolute path of a "
                                                                               "measurement file."))}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::openPattern(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::exportLayoutTool()
{
    McpTool tool;
    tool.name = QStringLiteral("export_layout");
    tool.description =
        QStringLiteral("Exports the pattern pieces to files. mode \"layout\" (default) arranges all pieces on pages "
                       "of the chosen paper, as for printing or cutting; mode \"pieces\" exports every piece on "
                       "its own. Needs at least one piece (add_piece). 'directory' (absolute) is where the files "
                       "go; they are named after 'name' (default: the pattern's file name) and numbered per page. "
                       "Seamly2D asks the user to confirm the folder first (and warns if files would be replaced). "
                       "A piece that does not fit the paper is reported before anything is written. If pieces "
                       "that would fit together end up on separate pages, allow 'rotate' or reduce 'margin' and "
                       "'gap'. The result lists the written files and the number of pages.");
    QJsonObject properties;
    properties.insert(QStringLiteral("directory"),
                      stringProperty(QStringLiteral("Absolute path of the folder to write to (it may be created if "
                                                    "its parent exists).")));
    properties.insert(QStringLiteral("name"),
                      stringProperty(QStringLiteral("Base file name without extension or folder.")));
    properties.insert(QStringLiteral("format"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"),
                                   QJsonArray{QStringLiteral("pdf"), QStringLiteral("svg"), QStringLiteral("png"),
                                              QStringLiteral("jpg"), QStringLiteral("tif"), QStringLiteral("eps"),
                                              QStringLiteral("ps"), QStringLiteral("dxf"),
                                              QStringLiteral("dxf_aama"), QStringLiteral("dxf_astm")}},
                                  {QStringLiteral("description"), QStringLiteral("File format, default pdf.")}});
    properties.insert(QStringLiteral("mode"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("layout"),
                                                                      QStringLiteral("pieces")}},
                                  {QStringLiteral("description"),
                                   QStringLiteral("layout = arranged on pages (default); pieces = one file per "
                                                  "piece.")}});
    properties.insert(QStringLiteral("paper"),
                      stringProperty(QStringLiteral("Layout mode: page size A0, A1, A2, A3, A4 (default), Letter, "
                                                    "Legal or Tabloid.")));
    properties.insert(QStringLiteral("paper_width"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                  {QStringLiteral("description"),
                                   QStringLiteral("Custom page width in paper_unit (instead of 'paper'; needs "
                                                  "paper_height).")}});
    properties.insert(QStringLiteral("paper_height"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                  {QStringLiteral("description"), QStringLiteral("Custom page height in paper_unit.")}});
    properties.insert(QStringLiteral("paper_unit"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("mm"), QStringLiteral("cm"),
                                                                      QStringLiteral("inch")}},
                                  {QStringLiteral("description"), QStringLiteral("Unit of paper_width, paper_height "
                                                                                 "and margin (default mm).")}});
    properties.insert(QStringLiteral("margin"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                  {QStringLiteral("description"), QStringLiteral("Page margin on all sides, in "
                                                                                 "paper_unit.")}});
    properties.insert(QStringLiteral("gap"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                  {QStringLiteral("description"), QStringLiteral("Space between pieces on a page, "
                                                                                 "in mm.")}});
    properties.insert(QStringLiteral("rotate"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                                  {QStringLiteral("description"), QStringLiteral("Allow rotating pieces to save "
                                                                                 "space.")}});
    properties.insert(QStringLiteral("crop"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                                  {QStringLiteral("description"), QStringLiteral("Crop each page to its content.")}});
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), properties},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("directory")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::exportLayout(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::screenshotTool()
{
    McpTool tool;
    tool.name = QStringLiteral("screenshot");
    tool.description =
        QStringLiteral("Returns an image of what is drawn, so that you can check your work visually. Shows the view "
                       "that is currently active (draft drawing, pattern pieces or layout; switch with show_mode). "
                       "area \"drawing\" (default) renders everything, independent of the user's zoom; \"window\" "
                       "returns what the user currently sees. The image is at most max_size pixels on its longer "
                       "side (default 1000). Does not change the pattern.");
    tool.inputSchema = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("area"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("drawing"),
                                                                      QStringLiteral("window")}}}},
                     {QStringLiteral("max_size"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                  {QStringLiteral("minimum"), 200}, {QStringLiteral("maximum"), 2000}}}}}};
    tool.readOnly = true;
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::screenshot(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::updatePieceTool()
{
    McpTool tool;
    tool.name = QStringLiteral("update_piece");
    tool.description =
        QStringLiteral("Changes the properties of a pattern piece (id from list_pieces); give only what changes. "
                       "'quantity' is how many pieces to cut, 'on_fold' marks a piece that is cut on the fold, "
                       "'letter' and 'annotation' are shown on the piece label, 'in_layout' decides whether the piece "
                       "is placed in the layout, 'seam_allowance' / 'seam_allowance_width' turn the seam allowance "
                       "on or change its width (pattern unit, formula allowed). One Undo step. Refused for locked "
                       "pieces, an unknown piece, a name that is taken, or an invalid value.");
    QJsonObject properties;
    properties.insert(QStringLiteral("piece_id"), idProperty(QStringLiteral("Piece id from list_pieces.")));
    properties.insert(QStringLiteral("name"), stringProperty(QStringLiteral("New unique name.")));
    properties.insert(QStringLiteral("quantity"), idProperty(QStringLiteral("How many to cut (1-999).")));
    properties.insert(QStringLiteral("letter"), stringProperty(QStringLiteral("Letter on the piece label.")));
    properties.insert(QStringLiteral("annotation"), stringProperty(QStringLiteral("Annotation on the piece label.")));
    for (const char *key : {"on_fold", "in_layout", "seam_allowance"})
    {
        properties.insert(QLatin1String(key), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}});
    }
    properties.insert(QStringLiteral("seam_allowance_width"),
                      stringProperty(QStringLiteral("Seam allowance width, number or formula.")));
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), properties},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("piece_id")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::updatePiece(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::setGrainlineTool()
{
    McpTool tool;
    tool.name = QStringLiteral("set_grainline");
    tool.description =
        QStringLiteral("Shows a grainline (the direction of the fabric's threads) on a piece, or hides it with "
                       "visible=false. Defaults: angle 90 (vertical), length 60 % of the piece height, an arrow at "
                       "both ends, placed in the middle of the piece. 'angle' and 'length' (pattern unit) may be "
                       "formulas. One Undo step.");
    QJsonObject properties;
    properties.insert(QStringLiteral("piece_id"), idProperty(QStringLiteral("Piece id from list_pieces.")));
    properties.insert(QStringLiteral("visible"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                                                             {QStringLiteral("description"),
                                                              QStringLiteral("false hides the grainline (default "
                                                                             "true).")}});
    properties.insert(QStringLiteral("angle"), stringProperty(QStringLiteral("Direction in degrees, default 90.")));
    properties.insert(QStringLiteral("length"), stringProperty(QStringLiteral("Length, number or formula.")));
    properties.insert(QStringLiteral("arrows"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("both"), QStringLiteral("top"),
                                                                      QStringLiteral("bottom")}}});
    properties.insert(QStringLiteral("arrow_length"), stringProperty(QStringLiteral("Size of the arrow heads.")));
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), properties},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("piece_id")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::setGrainline(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::setNotchTool()
{
    McpTool tool;
    tool.name = QStringLiteral("set_notch");
    tool.description =
        QStringLiteral("Adds, changes or removes a notch (the small marks that match pieces when sewing) at a point "
                       "of a piece's outline. 'point_id' is the id of the drawing's point (the one used in "
                       "add_piece). Without further arguments a notch with the program's default type and size is "
                       "added; type is slit, t, v_internal, v_external, u, castle or diamond; 'count' 1-3; 'length' "
                       "and 'width' in the pattern unit; notch=false removes it. One Undo step.");
    QJsonObject properties;
    properties.insert(QStringLiteral("piece_id"), idProperty(QStringLiteral("Piece id from list_pieces.")));
    properties.insert(QStringLiteral("point_id"), idProperty(QStringLiteral("Id of the outline point.")));
    properties.insert(QStringLiteral("notch"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                                                           {QStringLiteral("description"),
                                                            QStringLiteral("false removes the notch (default true).")}});
    properties.insert(QStringLiteral("type"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray::fromStringList(kNotchTypes)}});
    properties.insert(QStringLiteral("sub_type"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray::fromStringList(kNotchSubTypes)},
                                  {QStringLiteral("description"),
                                   QStringLiteral("Direction of the notch: straightforward (default), along the "
                                                  "bisector of the corner, or at the intersection.")}});
    properties.insert(QStringLiteral("count"), idProperty(QStringLiteral("1, 2 or 3 marks.")));
    for (const char *key : {"length", "width"})
    {
        properties.insert(QLatin1String(key), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}});
    }
    for (const char *key : {"show_on_cutline", "show_on_seamline"})
    {
        properties.insert(QLatin1String(key), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}});
    }
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), properties},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("piece_id"),
                                                                           QStringLiteral("point_id")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::setNotch(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::setMeasurementTool()
{
    McpTool tool;
    tool.name = QStringLiteral("set_measurement");
    tool.description =
        QStringLiteral("Changes the value of a measurement (for example waist_circ) in the measurement file of the "
                       "pattern; everything that depends on it follows at once. 'value' is in the pattern unit. The "
                       "measurement file belongs to the person and is shared by every pattern that uses it, so "
                       "Seamly2D asks the user to confirm the change (and the first change to a file in a session "
                       "may therefore take a moment). One Undo step, which writes the old value back to the file. "
                       "Only individual measurement files, and only measurements that are stored in the file; see "
                       "list_variables for names and current values.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("name"),
                                                 stringProperty(QStringLiteral("Measurement name, e.g. waist_circ."))},
                                                {QStringLiteral("value"),
                                                 QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                             {QStringLiteral("description"),
                                                              QStringLiteral("New value in the pattern unit.")}}}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("name"), QStringLiteral("value")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::setMeasurement(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::undoTool()
{
    McpTool tool;
    tool.name = QStringLiteral("undo");
    tool.description = QStringLiteral("Undoes the most recent step on the undo stack (the same as Ctrl+Z; it can be a "
                                      "step the user made by hand), 'steps' times (default 1). The result lists what "
                                      "was undone. Use redo to bring it back.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("steps"), idProperty(QStringLiteral("How many steps "
                                                                                                     "(1-50)."))}}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::undoRedo(arguments, true); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::redoTool()
{
    McpTool tool;
    tool.name = QStringLiteral("redo");
    tool.description = QStringLiteral("Redoes the step that was undone last (the same as Ctrl+Y), 'steps' times "
                                      "(default 1).");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("steps"), idProperty(QStringLiteral("How many steps "
                                                                                                     "(1-50)."))}}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::undoRedo(arguments, false); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::listStandardMeasurementsTool()
{
    McpTool tool;
    tool.name = QStringLiteral("list_standard_measurements");
    tool.description =
        QStringLiteral("Lists the standard measurement names the program knows (waist_circ, hip_circ, height_knee, "
                       "...) with their code, label and description. Search with 'query' (matches name, label, "
                       "description and code, e.g. \"waist\" or \"leg\"); 'limit' caps the list (default 40). Use "
                       "the names in create_measurements and in formulas.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("query"), stringProperty(QStringLiteral("Text to search for."))},
                                                {QStringLiteral("limit"), idProperty(QStringLiteral("Maximum number of results (1-400)."))}}}};
    tool.readOnly = true;
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::listStandardMeasurements(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::createMeasurementsTool()
{
    McpTool tool;
    tool.name = QStringLiteral("create_measurements");
    tool.description =
        QStringLiteral("Creates a new individual measurement file (.smis) for a person, so that patterns can be drafted "
                       "from measurements: 'values' maps measurement names to numbers in 'unit' (default: the "
                       "pattern's unit, or cm). Names are the standard names (see list_standard_measurements) or "
                       "custom ones written @name. Never overwrites an existing file. Seamly2D asks the user to "
                       "confirm the new file. It does not change the pattern: use attach_measurements afterwards.");
    tool.inputSchema = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("path"), stringProperty(QStringLiteral("Absolute path of the new file; .smis is appended if missing."))},
                     {QStringLiteral("values"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("additionalProperties"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                                  {QStringLiteral("description"), QStringLiteral("Measurement name -> value.")}}},
                     {QStringLiteral("unit"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("mm"), QStringLiteral("cm"), QStringLiteral("inch")}}}},
                     {QStringLiteral("given_name"), stringProperty(QStringLiteral("Optional: the person's given name."))},
                     {QStringLiteral("family_name"), stringProperty(QStringLiteral("Optional: the person's family name."))}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("path"), QStringLiteral("values")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::createMeasurements(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::attachMeasurementsTool()
{
    McpTool tool;
    tool.name = QStringLiteral("attach_measurements");
    tool.description =
        QStringLiteral("Makes the open pattern use an individual measurement file (Measurements > Load Individual): "
                       "the file's measurements become available in formulas, and set_measurement can change them. "
                       "Needs a draft block; refused if the pattern already uses measurements that the file does not "
                       "contain. Seamly2D asks the user to confirm the file. Not on the undo stack; attach another "
                       "file to change it. Use create_measurements to make a file first.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("path"), stringProperty(QStringLiteral("Absolute path of the .smis file."))}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::attachMeasurements(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::addDraftBlockTool()
{
    McpTool tool;
    tool.name = QStringLiteral("add_draft_block");
    tool.description =
        QStringLiteral("Creates a new draft block (a drawing area with its own base point) and makes it the active "
                       "one. Use it to start a pattern (a pattern needs at least one draft block before points can "
                       "be added) or to add a further block. For the first block 'unit' sets the pattern unit, which "
                       "cannot be changed afterwards. The result contains the base point's id, name and position; "
                       "draw the other points relative to it. One Undo step.");
    QJsonObject properties;
    properties.insert(QStringLiteral("name"),
                      stringProperty(QStringLiteral("Optional block name; generated when omitted. Must be unique.")));
    properties.insert(QStringLiteral("unit"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("mm"), QStringLiteral("cm"),
                                                                      QStringLiteral("inch")}},
                                  {QStringLiteral("description"),
                                   QStringLiteral("Pattern unit, only for the first draft block (default mm).")}});
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), properties}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::addDraftBlock(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::listObjectsTool()
{
    McpTool tool;
    tool.name = QStringLiteral("list_objects");
    tool.description =
        QStringLiteral("Lists the objects (points, lines, curves, ...) of the active draft block in construction "
                       "order. Each entry has its element type, its stored definition (formulas, referenced ids) "
                       "and, for points, id, name and position (x, y in the pattern unit, y grows downwards). Use "
                       "the ids as references when drawing. Big patterns have hundreds of objects: use the "
                       "optional filters to list only what you need (they combine).");
    tool.inputSchema = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("element"),
                      stringProperty(QStringLiteral("Only this kind of element, e.g. \"point\", \"line\", "
                                                    "\"spline\", \"arc\", \"operation\"."))},
                     {QStringLiteral("type"),
                      stringProperty(QStringLiteral("Only this type, e.g. \"endLine\", \"alongLine\", "
                                                    "\"normal\"."))},
                     {QStringLiteral("name_contains"),
                      stringProperty(QStringLiteral("Only objects whose name contains this text "
                                                    "(case-insensitive)."))},
                     {QStringLiteral("ids"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                  {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"),
                                                                         QStringLiteral("integer")}}},
                                  {QStringLiteral("description"), QStringLiteral("Only these object ids.")}}}}}};
    tool.readOnly = true;
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::listObjects(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::addPointTool()
{
    McpTool tool;
    tool.name = QStringLiteral("add_point");
    tool.description =
        QStringLiteral("Adds one point to the active draft block. It appears on the canvas immediately and is one "
                       "Undo step. Lengths are in the pattern unit and may be formulas using measurements and "
                       "increments. Angles are in degrees. Types:\n"
                       "- endLine: point at 'length' and 'angle' from 'base_point_id' (angle 0 = right, 90 = up, "
                       "180 = left, 270 = down on the screen).\n"
                       "- alongLine: point on the line from 'base_point_id' towards 'second_point_id' at distance "
                       "'length' from the base point ('CurrentLength' = the whole line length can be used in the "
                       "formula).\n"
                       "- normal: point at 'length' from 'base_point_id' on the perpendicular to the line "
                       "'base_point_id' -> 'second_point_id'. A positive length goes to the left of the direction "
                       "base -> second as seen on the screen (up for a line pointing right), a negative length to "
                       "the right; optional 'angle' turns that direction further (default 0).\n"
                       "- bisector: point at 'length' from 'vertex_point_id' on the bisector of the angle "
                       "'first_point_id' - 'vertex_point_id' - 'third_point_id'.\n"
                       "- lineIntersect: crossing point of the line 'line1_point1_id'-'line1_point2_id' with the "
                       "line 'line2_point1_id'-'line2_point2_id' (no length; the lines must not be parallel).\n"
                       "- height: foot of the perpendicular from 'base_point_id' onto the line "
                       "'line_point1_id'-'line_point2_id' (the line is extended if needed).\n"
                       "- lineIntersectAxis: where the line 'first_point_id'-'second_point_id' is crossed by an axis "
                       "through 'base_point_id' at 'angle' degrees (0 = horizontal to the right).\n"
                       "- curveIntersectAxis: where the curve or arc 'curve_id' is crossed by an axis through "
                       "'base_point_id' at 'angle' degrees.\n"
                       "- cutCurve: the point at 'length' along the spline, path or arc 'curve_id', counted from its "
                       "start ('from' \"start\", default) or its end (\"end\"); 0 < length < curve length.\n"
                       "- shoulder: on the line 'line_point1_id' -> 'line_point2_id', beyond line_point2, the point "
                       "whose distance to 'shoulder_point_id' is 'length' (the classic shoulder point).\n"
                       "- triangle: a point X on the axis 'axis_point1_id' -> 'axis_point2_id' from which the two points "
                       "'first_point_id' and 'second_point_id' are seen at a right angle (90 degrees at X). The axis "
                       "must cross the segment between the two points; X is found by walking from that crossing in "
                       "the direction axis_point1 -> axis_point2, so swapping the axis points gives the mirror "
                       "solution on the other side.\n"
                       "- intersectXY: the point with the x of 'x_from_point_id' and the y of 'y_from_point_id'.\n"
                       "- intersectCircles: where the circle around 'center1_point_id' with 'radius1' meets the circle "
                       "around 'center2_point_id' with 'radius2'; 'crossing' \"first\" (default) or \"second\" "
                       "chooses one of the two intersections.\n"
                       "- intersectCircleLine: where the circle around 'center_point_id' with 'radius' meets the line "
                       "'line_point1_id'-'line_point2_id' (the one on the segment if only one is).\n"
                       "- tangentToCircle: the point where the tangent from 'tangent_point_id' touches the circle "
                       "around 'center_point_id' with 'radius'; 'crossing' picks one of the two tangents.\n"
                       "- intersectArcs: where the two arcs 'first_arc_id' and 'second_arc_id' meet; 'crossing' "
                       "picks one of two intersections.\n"
                       "- tangentToArc: the point where the tangent from 'tangent_point_id' touches the arc "
                       "'arc_id'; 'crossing' picks one of the two tangents.\n"
                       "- intersectCurves: where the curves or arcs 'first_curve_id' and 'second_curve_id' cross; if "
                       "they cross more than once 'vertical' (highest, lowest) and 'horizontal' (leftmost, "
                       "rightmost) choose which crossing.\n"
                       "Call list_objects first to get the ids. Errors (unknown id, bad formula) leave the pattern "
                       "unchanged. Points are not connected by lines unless the tool draws one to its base point; "
                       "use add_line for further edges.");
    QJsonObject properties;
    properties.insert(QStringLiteral("type"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray::fromStringList(
                                       QStringList{VToolEndLine::ToolType, VToolAlongLine::ToolType,
                                                   VToolNormal::ToolType, VToolBisector::ToolType,
                                                   VToolLineIntersect::ToolType} + kAdvancedPointTypes)},
                                  {QStringLiteral("description"), QStringLiteral("Kind of point to add.")}});
    properties.insert(QStringLiteral("base_point_id"),
                      idProperty(QStringLiteral("endLine, alongLine, normal: id of the point to start from.")));
    properties.insert(QStringLiteral("second_point_id"),
                      idProperty(QStringLiteral("alongLine, normal: id of the point that defines the line "
                                                "direction.")));
    properties.insert(QStringLiteral("first_point_id"), idProperty(QStringLiteral("bisector: first angle point.")));
    properties.insert(QStringLiteral("vertex_point_id"), idProperty(QStringLiteral("bisector: angle vertex.")));
    properties.insert(QStringLiteral("third_point_id"), idProperty(QStringLiteral("bisector: third angle point.")));
    properties.insert(QStringLiteral("line1_point1_id"), idProperty(QStringLiteral("lineIntersect: first line, point 1.")));
    properties.insert(QStringLiteral("line1_point2_id"), idProperty(QStringLiteral("lineIntersect: first line, point 2.")));
    properties.insert(QStringLiteral("line2_point1_id"), idProperty(QStringLiteral("lineIntersect: second line, point 1.")));
    properties.insert(QStringLiteral("line2_point2_id"), idProperty(QStringLiteral("lineIntersect: second line, point 2.")));
    for (const char *key : {"line_point1_id", "line_point2_id", "shoulder_point_id", "axis_point1_id",
                            "axis_point2_id", "x_from_point_id", "y_from_point_id", "curve_id", "center1_point_id",
                            "center2_point_id", "center_point_id", "tangent_point_id", "first_arc_id",
                            "second_arc_id", "arc_id", "first_curve_id", "second_curve_id"})
    {
        properties.insert(QLatin1String(key), idProperty(QStringLiteral("Used by some of the types above (see the "
                                                                         "type's description).")));
    }
    properties.insert(QStringLiteral("from"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("start"), QStringLiteral("end")}},
                                  {QStringLiteral("description"), QStringLiteral("cutCurve: count the length from "
                                                                                 "the start (default) or the end.")}});
    for (const char *key : {"radius", "radius1", "radius2"})
    {
        properties.insert(QLatin1String(key), stringProperty(QStringLiteral("Radius of the circle (circle types), "
                                                                             "number or formula, greater than 0.")));
    }
    properties.insert(QStringLiteral("crossing"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("first"), QStringLiteral("second")}},
                                  {QStringLiteral("description"), QStringLiteral("Circle/arc/tangent types: which of "
                                                                                 "the two solutions (default "
                                                                                 "first).")}});
    properties.insert(QStringLiteral("vertical"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("highest"), QStringLiteral("lowest")}}});
    properties.insert(QStringLiteral("horizontal"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("leftmost"), QStringLiteral("rightmost")}}});
    properties.insert(QStringLiteral("length"),
                      stringProperty(QStringLiteral("endLine, alongLine, normal, bisector, cutCurve, shoulder: number "
                                                    "or formula in the pattern unit. Example: \"10\".")));
    properties.insert(QStringLiteral("angle"),
                      stringProperty(QStringLiteral("endLine (required), normal (optional), lineIntersectAxis and "
                                                    "curveIntersectAxis (required): number or formula in degrees. "
                                                    "Example: \"270\" (straight down).")));
    properties.insert(QStringLiteral("name"),
                      stringProperty(QStringLiteral("Optional point name; generated (A1, A2, ...) when omitted.")));
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), properties},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("type")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::addPoint(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::addLineTool()
{
    McpTool tool;
    tool.name = QStringLiteral("add_line");
    tool.description =
        QStringLiteral("Draws a straight line between two existing points of the active draft block. It appears on "
                       "the canvas immediately and is one Undo step. Some points already draw a line to their base "
                       "point (endLine, alongLine, normal, bisector, drawn from the base or vertex); a second line "
                       "between the same two points is refused.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"),
                                    QJsonObject{{QStringLiteral("first_point_id"),
                                                 idProperty(QStringLiteral("Id of the first point."))},
                                                {QStringLiteral("second_point_id"),
                                                 idProperty(QStringLiteral("Id of the second point."))}}},
                                   {QStringLiteral("required"), QJsonArray{QStringLiteral("first_point_id"),
                                                                           QStringLiteral("second_point_id")}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::addLine(arguments); };
    return tool;
}

//---------------------------------------------------------------------------------------------------------------------
McpTool PatternEditor::fitViewTool()
{
    McpTool tool;
    tool.name = QStringLiteral("fit_view");
    tool.description = QStringLiteral("Zooms the canvas so that the user sees what you worked on; call it after "
                                      "drawing. target \"all\" (default) is the toolbar's Zoom fit: everything in "
                                      "the current view, all draft blocks. \"block\" is Zoom selected in the draft "
                                      "view: only the active draft block. \"piece\" zooms to one piece in the piece "
                                      "view (give piece_id from list_pieces). Does not change the pattern.");
    tool.inputSchema = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("target"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("all"), QStringLiteral("block"),
                                                                      QStringLiteral("piece")}}}},
                     {QStringLiteral("piece_id"),
                      idProperty(QStringLiteral("target \"piece\": id of the piece to show."))}}}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::fitView(arguments); };
    return tool;
}
