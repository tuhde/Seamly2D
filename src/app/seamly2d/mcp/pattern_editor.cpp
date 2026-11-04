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
#include "../../libs/vgeometry/vcubicbezier.h"
#include "../../libs/vgeometry/vpointf.h"
#include "../../libs/vpatterndb/calculator.h"
#include "../../libs/vpatterndb/vcontainer.h"
#include "../../libs/vpatterndb/variables/vinternalvariable.h"
#include "../../libs/vpatterndb/variables/custom_variable.h"
#include "../../libs/vpatterndb/variables/vlinelength.h"
#include "../../libs/vtools/tools/drawTools/vtoolline.h"
#include "../../libs/vtools/tools/drawTools/toolcurve/vtoolarc.h"
#include "../../libs/vtools/tools/drawTools/toolcurve/vtoolcubicbezier.h"
#include "../../libs/vtools/tools/drawTools/toolcurve/vtoolspline.h"
#include "../../libs/vtools/tools/pattern_piece_tool.h"
#include "../../libs/vtools/tools/vabstracttool.h"
#include "../../libs/vpatterndb/vpiece.h"
#include "../../libs/vpatterndb/vpiecenode.h"
#include "../../libs/vpatterndb/vpiecepath.h"
#include "../../libs/vtools/undocommands/deletepiece.h"
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

namespace
{
/// What kind of value an editable attribute holds.
enum class AttrKind { Text, Formula, Number, Reference };

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
    const QStringList supported{QStringLiteral("spline"), VToolCubicBezier::ToolType};
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
                       "vertex_point_id, third_point_id, line1_point1_id, ...), and x/y for the base point of a "
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
                            "line1_point1_id", "line1_point2_id", "line2_point1_id", "line2_point2_id"})
    {
        properties.insert(QLatin1String(key), idProperty(QStringLiteral("New referenced point id (an earlier "
                                                                         "object).")));
    }
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
                       "Errors leave the pattern unchanged.");
    QJsonObject properties;
    properties.insert(QStringLiteral("type"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                  {QStringLiteral("enum"), QJsonArray{QStringLiteral("spline"),
                                                                      VToolCubicBezier::ToolType}},
                                  {QStringLiteral("description"), QStringLiteral("Kind of curve.")}});
    properties.insert(QStringLiteral("start_point_id"), idProperty(QStringLiteral("Id of the start point.")));
    properties.insert(QStringLiteral("end_point_id"), idProperty(QStringLiteral("Id of the end point.")));
    properties.insert(QStringLiteral("control1_point_id"),
                      idProperty(QStringLiteral("cubicBezier: id of the first control point.")));
    properties.insert(QStringLiteral("control2_point_id"),
                      idProperty(QStringLiteral("cubicBezier: id of the second control point.")));
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
                                   {QStringLiteral("required"),
                                    QJsonArray{QStringLiteral("type"), QStringLiteral("start_point_id"),
                                               QStringLiteral("end_point_id")}}};
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
