/***************************************************************************
 **  @file   mcp_integration.h
 **
 **  @brief  Owns the MCP server of a Seamly2D window and registers its tools.
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

#ifndef MCP_INTEGRATION_H
#define MCP_INTEGRATION_H

#include <QObject>
#include <QString>

#include "../../libs/vmcp/mcptoolregistry.h"

class McpServer;
class MainWindow;

/**
 * @brief The McpIntegration class connects the protocol library (vmcp) to one Seamly2D main window.
 *
 * It owns the server and the tool registry, registers the tools and reports the connection state to the UI.
 * The feature is opt-in: nothing listens unless the user enables it (or a test hook forces it).
 */
class McpIntegration : public QObject
{
    Q_OBJECT
public:
    explicit McpIntegration(MainWindow *window);
    ~McpIntegration() override;

    /// Starts the server from the saved settings when enabled there (or forced by the SEAMLY_MCP* test variables).
    void startFromSettings();

    bool    start(quint16 port);
    void    stop();
    bool    isRunning() const;
    quint16 port() const;
    QString token() const;
    QString regenerateToken();
    QString lastError() const;
    QString clientName() const;     ///< name reported by the last client that completed `initialize`

    /// Ready-to-paste configuration for Claude Code / Claude Desktop (`.mcp.json` content).
    QString mcpJsonSnippet() const;
    /// `claude mcp add ...` command line for Claude Code.
    QString claudeCodeCommand() const;
    /// Short text for the status bar.
    QString statusText() const;

signals:
    void stateChanged();

private:
    void registerTools();
    McpToolResult getPatternInfo(const QJsonObject &arguments);

    MainWindow      *m_window;
    McpToolRegistry  m_registry;
    McpServer       *m_server;
    QString          m_clientName;
};

#endif // MCP_INTEGRATION_H
