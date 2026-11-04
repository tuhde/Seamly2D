/***************************************************************************
 **  @file   mcptoolregistry.cpp
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

#include "mcptoolregistry.h"

#include <QJsonDocument>
#include <exception>

//---------------------------------------------------------------------------------------------------------------------
McpToolResult McpToolResult::ok(const QString &text, const QJsonValue &structured)
{
    McpToolResult result;
    result.text = text;
    result.structured = structured;
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult McpToolResult::error(const QString &text)
{
    McpToolResult result;
    result.isError = true;
    result.text = text;
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
bool McpToolRegistry::registerTool(const McpTool &tool)
{
    if (tool.name.isEmpty() || !tool.handler)
    {
        return false;
    }
    for (McpTool &existing : m_tools)
    {
        if (existing.name == tool.name)
        {
            existing = tool;
            return true;
        }
    }
    m_tools.append(tool);
    return true;
}

//---------------------------------------------------------------------------------------------------------------------
bool McpToolRegistry::contains(const QString &name) const
{
    for (const McpTool &tool : m_tools)
    {
        if (tool.name == name)
        {
            return true;
        }
    }
    return false;
}

//---------------------------------------------------------------------------------------------------------------------
int McpToolRegistry::size() const
{
    return m_tools.size();
}

//---------------------------------------------------------------------------------------------------------------------
QJsonArray McpToolRegistry::toolList() const
{
    QJsonArray list;
    for (const McpTool &tool : m_tools)
    {
        QJsonObject schema = tool.inputSchema;
        if (schema.isEmpty())
        {
            schema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
        }

        QJsonObject entry;
        entry.insert(QStringLiteral("name"), tool.name);
        entry.insert(QStringLiteral("description"), tool.description);
        entry.insert(QStringLiteral("inputSchema"), schema);
        entry.insert(QStringLiteral("annotations"),
                     QJsonObject{{QStringLiteral("readOnlyHint"), tool.readOnly}});
        list.append(entry);
    }
    return list;
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult McpToolRegistry::call(const QString &name, const QJsonObject &arguments) const
{
    for (const McpTool &tool : m_tools)
    {
        if (tool.name != name)
        {
            continue;
        }
        try
        {
            return tool.handler(arguments);
        }
        catch (const std::exception &e)
        {
            return McpToolResult::error(QStringLiteral("Tool '%1' failed: %2").arg(name, QString::fromUtf8(e.what())));
        }
        catch (...)
        {
            return McpToolResult::error(QStringLiteral("Tool '%1' failed with an unknown error.").arg(name));
        }
    }
    return McpToolResult::error(QStringLiteral("Unknown tool '%1'.").arg(name));
}
