/***************************************************************************
 **  @file   pattern_editor.h
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

#ifndef PATTERN_EDITOR_H
#define PATTERN_EDITOR_H

#include <QJsonObject>

#include "../../libs/vmcp/mcptoolregistry.h"

/**
 * @brief The PatternEditor class implements the MCP tools that read and draw in the open pattern.
 *
 * All functions must run in the GUI thread. Changes go through the same tool classes and undo commands as the
 * dialogs, so they show up on the canvas at once and can be undone. Every argument is validated before anything is
 * changed; on error the pattern stays untouched.
 */
class PatternEditor
{
public:
    /// Tool `list_objects`: the objects of the active draft block in construction order.
    static McpToolResult listObjects(const QJsonObject &arguments);

    /// Tool `add_point`: adds one point (endLine, alongLine, normal, bisector, lineIntersect) as one undo step.
    static McpToolResult addPoint(const QJsonObject &arguments);

    /// Tool `add_line`: draws a line between two existing points as one undo step.
    static McpToolResult addLine(const QJsonObject &arguments);

    /// Tool `fit_view`: zooms the canvas so that the whole drawing is visible.
    static McpToolResult fitView(const QJsonObject &arguments);

    /// Tool `delete_object`: deletes a point or line that nothing else uses.
    static McpToolResult deleteObject(const QJsonObject &arguments);

    /// Tool `update_object`: changes parameters (formulas, references, name, base point position) of an object.
    static McpToolResult updateObject(const QJsonObject &arguments);

    /// Tools `list_variables`, `set_variable`, `delete_variable`: custom variables (increments) and measurements.
    static McpToolResult listVariables(const QJsonObject &arguments);
    static McpToolResult setVariable(const QJsonObject &arguments);
    static McpToolResult deleteVariable(const QJsonObject &arguments);

    /// Tools `add_curve` (spline, cubicBezier) and `add_arc`: curves between existing points, one undo step each.
    static McpToolResult addCurve(const QJsonObject &arguments);
    static McpToolResult addArc(const QJsonObject &arguments);

    /// Tools `add_piece`, `list_pieces`, `delete_piece` and `show_mode` (draft or piece view).
    static McpToolResult addPiece(const QJsonObject &arguments);
    static McpToolResult listPieces(const QJsonObject &arguments);
    static McpToolResult deletePiece(const QJsonObject &arguments);
    static McpToolResult showMode(const QJsonObject &arguments);

    /// Tools `save_pattern` and `open_pattern` (the user is asked to confirm file access).
    static McpToolResult savePattern(const QJsonObject &arguments);
    static McpToolResult openPattern(const QJsonObject &arguments);

    /// Tool `add_draft_block`: creates a draft block with its base point (for the first block also sets the unit).
    static McpToolResult addDraftBlock(const QJsonObject &arguments);

    static McpTool listObjectsTool();
    static McpTool addPointTool();
    static McpTool addDraftBlockTool();
    static McpTool savePatternTool();
    static McpTool openPatternTool();
    static McpTool addPieceTool();
    static McpTool listPiecesTool();
    static McpTool deletePieceTool();
    static McpTool showModeTool();
    static McpTool addCurveTool();
    static McpTool addArcTool();
    static McpTool listVariablesTool();
    static McpTool setVariableTool();
    static McpTool deleteVariableTool();
    static McpTool deleteObjectTool();
    static McpTool updateObjectTool();
    static McpTool addLineTool();
    static McpTool fitViewTool();
};

#endif // PATTERN_EDITOR_H
