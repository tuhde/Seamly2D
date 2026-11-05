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

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>

#include "../../libs/vmcp/mcpinstances.h"
#include "../../libs/vmcp/mcptoolregistry.h"

class McpRouter;
class McpServer;
class MainWindow;

/**
 * @brief The McpIntegration class connects the protocol library (vmcp) to one Seamly2D main window.
 *
 * It owns the servers and the tool registry, registers the tools and reports the connection state and a log to the UI.
 * The feature is opt-in: nothing listens unless the user enables it (or a test hook forces it).
 *
 * Every Seamly2D window is its own process, but Claude is configured with one URL and one token only. So each window
 * runs a private server (random port, reached only by the other windows) and tries to also own the public port. The
 * window that gets it is the hub: it answers the clients and routes every tool call to the window the client means
 * (see McpRouter). When the hub window closes, another window takes the public port over.
 */
class McpIntegration : public QObject
{
    Q_OBJECT
public:
    /// A client counts as connected for this long after its last request.
    static constexpr int ActiveSeconds = 90;

    explicit McpIntegration(MainWindow *window);
    ~McpIntegration() override;

    /// Starts the server from the saved settings when enabled there (or forced by the SEAMLY_MCP* test variables).
    void startFromSettings();

    /// Starts this window's server and tries to become the hub on `port` (0 = any free port).
    bool    start(quint16 port);
    void    stop();
    bool    isRunning() const;
    /// True when this window owns the public port and answers the clients.
    bool    isHub() const;
    /// The public port the clients connect to (the configured one until a window owns it).
    quint16 port() const;
    QString token() const;
    /// Replaces the saved token. All clients have to be configured again.
    QString regenerateToken();
    QString lastError() const;
    QString clientName() const;     ///< name reported by the last client that completed `initialize`

    /// All open Seamly2D windows (this one included).
    QList<McpInstance> windows() const;
    qint64 windowId() const;

    /// Ready-to-paste configuration for Claude Code / Claude Desktop (`.mcp.json` content).
    QString mcpJsonSnippet() const;
    /// `claude mcp add ...` command line for Claude Code.
    QString claudeCodeCommand() const;
    /// Short text for the status bar.
    QString statusText() const;
    /// Whether a client sent a request lately. HTTP has no connection to watch, so this is how "connected" is told.
    bool    clientActive() const;
    /// Text for the panel: active client, or when the last one was seen.
    QString clientText() const;

signals:
    void stateChanged();
    /// A line for the log in the MCP panel (without time stamp).
    void logMessage(const QString &message);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void registerTools();
    McpToolResult getPatternInfo(const QJsonObject &arguments);
    void ensureToken();
    void adoptSavedToken();
    void tryBecomeHub();
    void publishWindow();
    void tick();
    void log(const QString &message);

    MainWindow      *m_window;
    McpToolRegistry  m_registry;          ///< the tools of this window
    McpServer       *m_server;            ///< private port: reached by the hub only
    McpToolRegistry  m_hubRegistry;       ///< m_registry's tools plus `pattern` routing
    McpServer       *m_hubServer;         ///< public port: the clients
    McpInstanceRegistry m_instances;
    std::unique_ptr<McpRouter> m_router;
    QTimer           m_timer;
    QString          m_clientName;
    QDateTime        m_lastRequest;      ///< last authenticated request on the public port
    bool             m_wasActive = false;
    quint16          m_publicPort = 0;
    qint64           m_lastActivated = 0;
    bool             m_running = false;
    bool             m_tokenFromEnvironment = false;
    bool             m_loggedPortBusy = false;
    QList<McpInstance> m_knownWindows;
};

#endif // MCP_INTEGRATION_H
