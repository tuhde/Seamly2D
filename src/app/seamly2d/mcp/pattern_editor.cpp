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

#include <QDomElement>
#include <QDomNamedNodeMap>
#include <QHash>
#include <QJsonArray>
#include <QSharedPointer>
#include <QUndoStack>
#include <QtMath>

#include "../core/application_2d.h"
#include "../mainwindow.h"
#include "../../libs/ifc/exception/vexception.h"
#include "../../libs/ifc/ifcdef.h"
#include "../../libs/ifc/xml/vabstractpattern.h"
#include "../../libs/qmuparser/qmuparsererror.h"
#include "../../libs/vgeometry/vpointf.h"
#include "../../libs/vpatterndb/calculator.h"
#include "../../libs/vpatterndb/vcontainer.h"
#include "../../libs/vpatterndb/variables/vinternalvariable.h"
#include "../../libs/vpatterndb/variables/vlinelength.h"
#include "../../libs/vtools/tools/drawTools/vtoolline.h"
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
    Q_UNUSED(arguments)
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

    QJsonArray objects;
    for (QDomNode node = calculation.firstChild(); !node.isNull(); node = node.nextSibling())
    {
        const QDomElement element = node.toElement();
        if (element.isNull())
        {
            continue;
        }
        QJsonObject item{{QStringLiteral("element"), element.tagName()},
                         {QStringLiteral("definition"), attributesOf(element)}};

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

    return McpToolResult::ok(QStringLiteral("%1 object(s) in draft block \"%2\" (unit: %3; y grows downwards). "
                                            "Use the ids as references.")
                                 .arg(objects.size())
                                 .arg(ctx.doc->getActiveDraftBlockName(), UnitsToStr(qApp->patternUnit())),
                             QJsonObject{{QStringLiteral("draftBlock"), ctx.doc->getActiveDraftBlockName()},
                                         {QStringLiteral("unit"), UnitsToStr(qApp->patternUnit())},
                                         {QStringLiteral("objects"), objects}});
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
    if (!supported.contains(type))
    {
        return McpToolResult::error(QStringLiteral("Unsupported point type \"%1\". Supported: %2.")
                                        .arg(type, supported.join(QStringLiteral(", "))));
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
    Q_UNUSED(arguments)
    VMainGraphicsView *view = qApp->getSceneView();
    if (view == nullptr)
    {
        return McpToolResult::error(QStringLiteral("No pattern is open in Seamly2D."));
    }
    view->zoomToFit();
    return McpToolResult::ok(QStringLiteral("Zoomed the canvas so that the whole drawing is visible."));
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
                       "the ids as references when drawing.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject()}};
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
                       "Call list_objects first to get the ids. Errors (unknown id, bad formula) leave the pattern "
                       "unchanged. Points are not connected by lines unless the tool draws one to its base point; "
                       "use add_line for further edges.");
    QJsonObject properties;
    properties.insert(QStringLiteral("type"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{VToolEndLine::ToolType, VToolAlongLine::ToolType,
                                                                      VToolNormal::ToolType, VToolBisector::ToolType,
                                                                      VToolLineIntersect::ToolType}},
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
    properties.insert(QStringLiteral("length"),
                      stringProperty(QStringLiteral("endLine, alongLine, normal, bisector: number or formula in the "
                                                    "pattern unit. Example: \"10\".")));
    properties.insert(QStringLiteral("angle"),
                      stringProperty(QStringLiteral("endLine (required), normal (optional): number or formula in "
                                                    "degrees. Example: \"270\" (straight down).")));
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
    tool.description = QStringLiteral("Zooms the canvas so that the whole drawing is visible. Call it after drawing "
                                      "so that the user sees everything. Does not change the pattern.");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject()}};
    tool.handler = [](const QJsonObject &arguments) { return PatternEditor::fitView(arguments); };
    return tool;
}
