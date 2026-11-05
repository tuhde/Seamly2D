/***************************************************************************
 **  @file   mcprouter.h
 **
 **  @brief  Routes MCP tool calls to one of several Seamly2D windows.
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

#ifndef MCPROUTER_H
#define MCPROUTER_H

#include <QJsonObject>
#include <QString>
#include <functional>

#include "mcpinstances.h"
#include "mcptoolregistry.h"

/**
 * @brief The McpRouter class lets the window that owns the public MCP port (the hub) serve every open window.
 *
 * It wraps each tool of the window's own registry: the wrapper takes the optional `pattern` argument, picks the
 * target window and runs the tool there, locally or over the target's private MCP port. Without `pattern` the
 * window that was activated last is used. It also adds the `list_patterns` tool.
 */
class McpRouter
{
public:
    /// Called after each routed call: tool, target window, whether it failed, whether it ran in this window.
    using Logger = std::function<void(const QString &tool, const McpInstance &target, bool isError, bool local)>;

    McpRouter(const McpToolRegistry *local, const McpInstanceRegistry *instances, std::function<QString()> token);

    void setLogger(const Logger &logger) { m_logger = logger; }
    void setRemoteTimeout(int milliseconds) { m_remoteTimeoutMs = milliseconds; }

    /// Registers the wrapped tools and `list_patterns` in `routed`.
    void populate(McpToolRegistry *routed) const;

    struct Target
    {
        bool        ok = false;
        McpInstance instance;
        QString     error;
    };
    /// Finds the window for `pattern` (pid, file path, file name or title); empty = the one activated last.
    Target resolve(const QString &pattern) const;

    /// Sends `tools/call` to another window's MCP server and converts the answer back to a result.
    static McpToolResult callRemote(quint16 port, const QString &token, const QString &tool,
                                    const QJsonObject &arguments, int timeoutMs);

    static QJsonObject describe(const McpInstance &instance, bool self, bool newest);

private:
    McpToolResult route(const QString &tool, QJsonObject arguments) const;
    McpToolResult listPatterns() const;

    const McpToolRegistry       *m_local;
    const McpInstanceRegistry   *m_instances;
    std::function<QString()>     m_token;
    Logger                       m_logger;
    int                          m_remoteTimeoutMs = 10 * 60 * 1000;   // a tool may wait for the user's dialog
};

#endif // MCPROUTER_H
