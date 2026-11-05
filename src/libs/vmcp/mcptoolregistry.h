/***************************************************************************
 **  @file   mcptoolregistry.h
 **
 **  @brief  Registry of tools offered over the Model Context Protocol (MCP).
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

#ifndef MCPTOOLREGISTRY_H
#define MCPTOOLREGISTRY_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>

/**
 * @brief The McpToolResult struct is what a tool handler returns.
 *
 * A failed call (invalid formula, unknown reference, ...) is reported to the client as a tool result with
 * isError set, not as a protocol error, so that the model can read the message and correct its call.
 */
struct McpToolResult
{
    bool    isError = false;
    QString text;               ///< Human/model readable message.
    QJsonValue structured;      ///< Optional machine readable payload; serialised into the text when set.
    QByteArray imagePng;        ///< Optional PNG image, returned to the client as an MCP image content block.

    static McpToolResult ok(const QString &text, const QJsonValue &structured = QJsonValue());
    static McpToolResult error(const QString &text);
};

/// Tool handler. Always invoked in the thread that owns the McpServer (the GUI thread).
using McpToolHandler = std::function<McpToolResult(const QJsonObject &arguments)>;

struct McpTool
{
    QString        name;
    QString        description;
    QJsonObject    inputSchema;   ///< JSON Schema (type "object").
    bool           readOnly = false;
    McpToolHandler handler;
};

/**
 * @brief The McpToolRegistry class holds the tool definitions and dispatches calls.
 */
class McpToolRegistry
{
public:
    /// Adds or replaces a tool. Returns false when the tool is invalid (empty name, no handler).
    bool registerTool(const McpTool &tool);
    bool contains(const QString &name) const;
    int  size() const;
    /// All registered tools (used to wrap them, e.g. for routing between windows).
    QVector<McpTool> tools() const { return m_tools; }

    /// The `tools` array for the `tools/list` response.
    QJsonArray toolList() const;

    /// Runs a tool. Unknown names and handler exceptions yield an error result.
    McpToolResult call(const QString &name, const QJsonObject &arguments) const;

private:
    QVector<McpTool> m_tools;
};

#endif // MCPTOOLREGISTRY_H
