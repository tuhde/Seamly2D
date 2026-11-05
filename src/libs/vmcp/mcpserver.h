/***************************************************************************
 **  @file   mcpserver.h
 **
 **  @brief  Model Context Protocol (MCP) server: JSON-RPC 2.0 over local HTTP.
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

#ifndef MCPSERVER_H
#define MCPSERVER_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTcpServer>

class McpToolRegistry;
class QTcpSocket;

/**
 * @brief The McpServer class is a Model Context Protocol server using the "Streamable HTTP" transport
 * (single endpoint, POST with a JSON-RPC message, JSON response).
 *
 * The server lives in, and calls tool handlers from, the thread that created it (the GUI thread), so handlers
 * may touch documents, scenes and the undo stack without extra locking.
 *
 * Security: binds to 127.0.0.1 only, requires `Authorization: Bearer <token>`, and rejects requests whose Host or
 * Origin header is not a loopback name (protection against DNS rebinding from web pages).
 */
class McpServer : public QObject
{
    Q_OBJECT
public:
    static const QString Endpoint;                 ///< "/mcp"
    static const QString LatestProtocolVersion;    ///< newest MCP revision this server speaks
    static constexpr int MaxHeaderBytes = 16 * 1024;
    static constexpr int MaxBodyBytes   = 4 * 1024 * 1024;
    static constexpr int ConnectionTimeoutMs = 15000;

    explicit McpServer(McpToolRegistry *registry, QObject *parent = nullptr);
    ~McpServer() override;

    /// Starts listening on 127.0.0.1. Port 0 picks a free port. Generates a token if none is set.
    bool    start(quint16 port = 0);
    void    stop();
    bool    isListening() const;
    quint16 port() const;
    QString errorString() const;

    QString token() const;
    void    setToken(const QString &token);
    /// Replaces the token with a new random one (existing clients lose access).
    QString regenerateToken();

    void setServerInfo(const QString &name, const QString &title, const QString &version);
    /// Text sent to the client in the `initialize` result (working model, conventions).
    void setInstructions(const QString &instructions);
    /// How long a connection may stay idle before it is closed (default ConnectionTimeoutMs; for tests).
    void setConnectionTimeout(int milliseconds) { m_connectionTimeoutMs = milliseconds; }

    /// Handles one JSON-RPC message (request, notification or response). Returns the reply, or a null
    /// value when no reply is due (notifications and responses). Public so protocol tests need no sockets.
    QJsonValue handleMessage(const QJsonObject &message);

signals:
    void listeningChanged(bool listening);
    /// A client completed `initialize`.
    void clientInitialized(const QString &clientName, const QString &clientVersion);
    /// An authenticated JSON-RPC request arrived (any method). Lets the UI tell whether a client is still active:
    /// HTTP has no connection to watch, and clients do not say goodbye.
    void requestReceived(const QString &method);
    /// A tool is about to run / has run (for status display and logging).
    void toolCalled(const QString &toolName, bool isError);

private slots:
    void onNewConnection();

private:
    struct Connection
    {
        QByteArray buffer;
        bool       processing = false;   ///< a complete request is being handled (a tool may wait for a dialog)
    };

    void readFromSocket(QTcpSocket *socket);
    void processRequest(QTcpSocket *socket, const QByteArray &requestLine, const QHash<QByteArray, QByteArray> &headers,
                        const QByteArray &body);
    void sendResponse(QTcpSocket *socket, int status, const QByteArray &body = QByteArray(),
                      const QList<QPair<QByteArray, QByteArray>> &extraHeaders = {});
    bool authorized(const QHash<QByteArray, QByteArray> &headers) const;
    static bool loopbackHost(const QByteArray &hostHeader);
    static bool loopbackOrigin(const QByteArray &origin);

    QJsonObject handleInitialize(const QJsonObject &params);
    QJsonObject handleToolsCall(const QJsonObject &params, QJsonObject *protocolError);

    McpToolRegistry *m_registry;
    QTcpServer       m_server;
    QHash<QTcpSocket *, Connection> m_connections;
    int              m_connectionTimeoutMs = ConnectionTimeoutMs;
    QString          m_token;
    QString          m_name         = QStringLiteral("mcp-server");
    QString          m_title;
    QString          m_version      = QStringLiteral("0");
    QString          m_instructions;
    bool             m_handling     = false;   ///< re-entrancy guard (a handler may spin an event loop)
};

#endif // MCPSERVER_H
