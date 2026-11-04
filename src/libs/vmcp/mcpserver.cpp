/***************************************************************************
 **  @file   mcpserver.cpp
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

#include "mcpserver.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>

#include "mcptoolregistry.h"

namespace
{
// JSON-RPC 2.0 error codes.
constexpr int ParseError     = -32700;
constexpr int InvalidRequest = -32600;
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams  = -32602;
constexpr int ServerBusy     = -32001;

const QStringList &supportedProtocolVersions()
{
    static const QStringList versions{QStringLiteral("2025-06-18"), QStringLiteral("2025-03-26"),
                                      QStringLiteral("2024-11-05")};
    return versions;
}

QJsonObject errorReply(const QJsonValue &id, int code, const QString &message)
{
    return QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
                                                             {QStringLiteral("message"), message}}}};
}

QJsonObject resultReply(const QJsonValue &id, const QJsonObject &result)
{
    return QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("result"), result}};
}

// Compares without leaking, through timing, how many leading characters matched.
bool constantTimeEqual(const QByteArray &a, const QByteArray &b)
{
    int diff = a.size() ^ b.size();
    const int n = qMin(a.size(), b.size());
    for (int i = 0; i < n; ++i)
    {
        diff |= (a.at(i) ^ b.at(i));
    }
    return diff == 0;
}

QByteArray reasonPhrase(int status)
{
    switch (status)
    {
        case 200: return "OK";
        case 202: return "Accepted";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        default:  return "Error";
    }
}
} // namespace

const QString McpServer::Endpoint              = QStringLiteral("/mcp");
const QString McpServer::LatestProtocolVersion = QStringLiteral("2025-06-18");

//---------------------------------------------------------------------------------------------------------------------
McpServer::McpServer(McpToolRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_server(this)
{
    connect(&m_server, &QTcpServer::newConnection, this, &McpServer::onNewConnection);
}

//---------------------------------------------------------------------------------------------------------------------
McpServer::~McpServer()
{
    stop();
}

//---------------------------------------------------------------------------------------------------------------------
bool McpServer::start(quint16 port)
{
    if (m_server.isListening())
    {
        return true;
    }
    if (m_token.isEmpty())
    {
        regenerateToken();
    }
    const bool ok = m_server.listen(QHostAddress::LocalHost, port);
    emit listeningChanged(ok);
    return ok;
}

//---------------------------------------------------------------------------------------------------------------------
void McpServer::stop()
{
    const bool wasListening = m_server.isListening();
    m_server.close();
    const auto sockets = m_connections.keys();
    for (QTcpSocket *socket : sockets)
    {
        socket->abort();
        socket->deleteLater();
    }
    m_connections.clear();
    if (wasListening)
    {
        emit listeningChanged(false);
    }
}

//---------------------------------------------------------------------------------------------------------------------
bool McpServer::isListening() const
{
    return m_server.isListening();
}

//---------------------------------------------------------------------------------------------------------------------
quint16 McpServer::port() const
{
    return m_server.serverPort();
}

//---------------------------------------------------------------------------------------------------------------------
QString McpServer::errorString() const
{
    return m_server.errorString();
}

//---------------------------------------------------------------------------------------------------------------------
QString McpServer::token() const
{
    return m_token;
}

//---------------------------------------------------------------------------------------------------------------------
void McpServer::setToken(const QString &token)
{
    m_token = token;
}

//---------------------------------------------------------------------------------------------------------------------
QString McpServer::regenerateToken()
{
    QByteArray raw(32, Qt::Uninitialized);
    QRandomGenerator *rng = QRandomGenerator::system();
    for (int i = 0; i < raw.size(); ++i)
    {
        raw[i] = static_cast<char>(rng->bounded(256));
    }
    m_token = QString::fromLatin1(raw.toHex());
    return m_token;
}

//---------------------------------------------------------------------------------------------------------------------
void McpServer::setServerInfo(const QString &name, const QString &title, const QString &version)
{
    m_name = name;
    m_title = title;
    m_version = version;
}

//---------------------------------------------------------------------------------------------------------------------
void McpServer::setInstructions(const QString &instructions)
{
    m_instructions = instructions;
}

//---------------------------------------------------------------------------------------------------------------------
void McpServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection())
    {
        m_connections.insert(socket, Connection());

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readFromSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]()
        {
            m_connections.remove(socket);
            socket->deleteLater();
        });

        // A client that connects and then stalls must not keep the socket (and its buffer) forever. A request that was
        // received completely is not stalled: its tool may wait for the user to answer a confirmation dialog.
        QTimer::singleShot(ConnectionTimeoutMs, socket, [this, socket]()
        {
            const auto it = m_connections.constFind(socket);
            if (it != m_connections.constEnd() && !it->processing)
            {
                socket->abort();
            }
        });
    }
}

//---------------------------------------------------------------------------------------------------------------------
void McpServer::readFromSocket(QTcpSocket *socket)
{
    auto it = m_connections.find(socket);
    if (it == m_connections.end())
    {
        return;
    }
    it->buffer.append(socket->readAll());

    const int headerEnd = it->buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
    {
        if (it->buffer.size() > MaxHeaderBytes)
        {
            sendResponse(socket, 431);
        }
        return;
    }
    if (headerEnd > MaxHeaderBytes)
    {
        sendResponse(socket, 431);
        return;
    }

    const QList<QByteArray> lines = it->buffer.left(headerEnd).split('\n');
    const QByteArray requestLine = lines.value(0).trimmed();
    QHash<QByteArray, QByteArray> headers;
    for (int i = 1; i < lines.size(); ++i)
    {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon > 0)
        {
            headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
        }
    }

    bool lengthOk = true;
    const QByteArray lengthHeader = headers.value("content-length");
    const qint64 contentLength = lengthHeader.isEmpty() ? 0 : lengthHeader.toLongLong(&lengthOk);
    if (!lengthOk || contentLength < 0 || headers.contains("transfer-encoding"))
    {
        sendResponse(socket, 400);   // chunked bodies are not supported
        return;
    }
    if (contentLength > MaxBodyBytes)
    {
        sendResponse(socket, 413);
        return;
    }

    const qint64 bodyStart = headerEnd + 4;
    if (it->buffer.size() < bodyStart + contentLength)
    {
        return;   // wait for the rest of the body
    }
    const QByteArray body = it->buffer.mid(static_cast<int>(bodyStart), static_cast<int>(contentLength));
    it->buffer.clear();
    it->processing = true;
    processRequest(socket, requestLine, headers, body);
    const auto after = m_connections.find(socket);   // the connection may be gone by now
    if (after != m_connections.end())
    {
        after->processing = false;
    }
}

//---------------------------------------------------------------------------------------------------------------------
bool McpServer::loopbackHost(const QByteArray &hostHeader)
{
    // "127.0.0.1", "127.0.0.1:1234", "localhost", "localhost:1234", "[::1]:1234"
    QByteArray host = hostHeader.trimmed().toLower();
    if (host.startsWith('['))
    {
        const int close = host.indexOf(']');
        host = close > 0 ? host.mid(1, close - 1) : host;
    }
    else
    {
        const int colon = host.indexOf(':');
        if (colon >= 0)
        {
            host = host.left(colon);
        }
    }
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

//---------------------------------------------------------------------------------------------------------------------
bool McpServer::loopbackOrigin(const QByteArray &origin)
{
    const QByteArray o = origin.trimmed().toLower();
    for (const QByteArray &scheme : {QByteArray("http://"), QByteArray("https://")})
    {
        if (o.startsWith(scheme))
        {
            return loopbackHost(o.mid(scheme.size()));
        }
    }
    return false;
}

//---------------------------------------------------------------------------------------------------------------------
bool McpServer::authorized(const QHash<QByteArray, QByteArray> &headers) const
{
    static const QByteArray prefix = "bearer ";
    const QByteArray value = headers.value("authorization");
    if (m_token.isEmpty() || value.size() <= prefix.size() || value.left(prefix.size()).toLower() != prefix)
    {
        return false;
    }
    return constantTimeEqual(value.mid(prefix.size()).trimmed(), m_token.toLatin1());
}

//---------------------------------------------------------------------------------------------------------------------
void McpServer::sendResponse(QTcpSocket *socket, int status, const QByteArray &body,
                             const QList<QPair<QByteArray, QByteArray>> &extraHeaders)
{
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasonPhrase(status) + "\r\n";
    if (!body.isEmpty())
    {
        out += "Content-Type: application/json\r\n";
    }
    for (const auto &header : extraHeaders)
    {
        out += header.first + ": " + header.second + "\r\n";
    }
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    out += body;

    socket->write(out);
    socket->disconnectFromHost();
}

//---------------------------------------------------------------------------------------------------------------------
void McpServer::processRequest(QTcpSocket *socket, const QByteArray &requestLine,
                               const QHash<QByteArray, QByteArray> &headers, const QByteArray &body)
{
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2)
    {
        sendResponse(socket, 400);
        return;
    }
    const QByteArray method = parts.at(0);
    QByteArray path = parts.at(1);
    const int query = path.indexOf('?');
    if (query >= 0)
    {
        path = path.left(query);
    }

    // DNS-rebinding protection comes first: nothing is revealed to non-loopback origins.
    if (!loopbackHost(headers.value("host")) ||
        (headers.contains("origin") && !loopbackOrigin(headers.value("origin"))))
    {
        sendResponse(socket, 403);
        return;
    }
    if (path != Endpoint.toLatin1())
    {
        sendResponse(socket, 404);
        return;
    }
    if (!authorized(headers))
    {
        sendResponse(socket, 401, QByteArray(), {{"WWW-Authenticate", "Bearer"}});
        return;
    }
    if (method != "POST")
    {
        // No server-initiated SSE stream is offered, so GET (and DELETE) are not allowed.
        sendResponse(socket, 405, QByteArray(), {{"Allow", "POST"}});
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || (!document.isObject() && !document.isArray()))
    {
        sendResponse(socket, 400,
                     QJsonDocument(errorReply(QJsonValue::Null, ParseError, QStringLiteral("Parse error"))).toJson(
                         QJsonDocument::Compact));
        return;
    }

    QJsonValue reply;
    if (document.isArray())
    {
        QJsonArray replies;
        const QJsonArray batch = document.array();
        for (const QJsonValue &item : batch)
        {
            const QJsonValue r = item.isObject()
                                     ? handleMessage(item.toObject())
                                     : QJsonValue(errorReply(QJsonValue::Null, InvalidRequest,
                                                             QStringLiteral("Invalid request")));
            if (!r.isNull() && !r.isUndefined())
            {
                replies.append(r);
            }
        }
        if (batch.isEmpty())
        {
            reply = errorReply(QJsonValue::Null, InvalidRequest, QStringLiteral("Invalid request"));
        }
        else if (!replies.isEmpty())
        {
            reply = replies;
        }
    }
    else
    {
        reply = handleMessage(document.object());
    }

    if (reply.isNull() || reply.isUndefined())
    {
        sendResponse(socket, 202);   // only notifications / responses were received
        return;
    }
    const QByteArray payload = reply.isArray() ? QJsonDocument(reply.toArray()).toJson(QJsonDocument::Compact)
                                               : QJsonDocument(reply.toObject()).toJson(QJsonDocument::Compact);
    sendResponse(socket, 200, payload);
}

//---------------------------------------------------------------------------------------------------------------------
QJsonValue McpServer::handleMessage(const QJsonObject &message)
{
    const bool hasId = message.contains(QStringLiteral("id"));
    const QJsonValue id = message.value(QStringLiteral("id"));
    const QString method = message.value(QStringLiteral("method")).toString();

    if (message.value(QStringLiteral("jsonrpc")).toString() != QLatin1String("2.0"))
    {
        return hasId ? QJsonValue(errorReply(id, InvalidRequest, QStringLiteral("jsonrpc must be \"2.0\"")))
                     : QJsonValue();
    }

    // A response to a server-initiated request: this server never sends any.
    if (method.isEmpty())
    {
        return (message.contains(QStringLiteral("result")) || message.contains(QStringLiteral("error")))
                   ? QJsonValue()
                   : QJsonValue(errorReply(id, InvalidRequest, QStringLiteral("Missing method")));
    }

    if (!hasId)
    {
        return QJsonValue();   // notification (e.g. notifications/initialized): nothing to answer
    }

    const QJsonObject params = message.value(QStringLiteral("params")).toObject();

    if (method == QLatin1String("initialize"))
    {
        return resultReply(id, handleInitialize(params));
    }
    if (method == QLatin1String("ping"))
    {
        return resultReply(id, QJsonObject());
    }
    if (method == QLatin1String("tools/list"))
    {
        return resultReply(id, QJsonObject{{QStringLiteral("tools"), m_registry->toolList()}});
    }
    if (method == QLatin1String("tools/call"))
    {
        QJsonObject protocolError;
        const QJsonObject result = handleToolsCall(params, &protocolError);
        if (!protocolError.isEmpty())
        {
            return errorReply(id, protocolError.value(QStringLiteral("code")).toInt(),
                              protocolError.value(QStringLiteral("message")).toString());
        }
        return resultReply(id, result);
    }
    return errorReply(id, MethodNotFound, QStringLiteral("Method not found: %1").arg(method));
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject McpServer::handleInitialize(const QJsonObject &params)
{
    const QString requested = params.value(QStringLiteral("protocolVersion")).toString();
    const QString version = supportedProtocolVersions().contains(requested) ? requested : LatestProtocolVersion;

    const QJsonObject clientInfo = params.value(QStringLiteral("clientInfo")).toObject();
    emit clientInitialized(clientInfo.value(QStringLiteral("name")).toString(),
                           clientInfo.value(QStringLiteral("version")).toString());

    QJsonObject serverInfo{{QStringLiteral("name"), m_name}, {QStringLiteral("version"), m_version}};
    if (!m_title.isEmpty())
    {
        serverInfo.insert(QStringLiteral("title"), m_title);
    }
    QJsonObject result{{QStringLiteral("protocolVersion"), version},
                       {QStringLiteral("capabilities"),
                        QJsonObject{{QStringLiteral("tools"), QJsonObject{{QStringLiteral("listChanged"), false}}}}},
                       {QStringLiteral("serverInfo"), serverInfo}};
    if (!m_instructions.isEmpty())
    {
        result.insert(QStringLiteral("instructions"), m_instructions);
    }
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject McpServer::handleToolsCall(const QJsonObject &params, QJsonObject *protocolError)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    if (name.isEmpty() || !m_registry->contains(name))
    {
        *protocolError = QJsonObject{{QStringLiteral("code"), InvalidParams},
                                     {QStringLiteral("message"),
                                      name.isEmpty() ? QStringLiteral("Missing tool name")
                                                     : QStringLiteral("Unknown tool: %1").arg(name)}};
        return QJsonObject();
    }
    const QJsonValue args = params.value(QStringLiteral("arguments"));
    if (!args.isUndefined() && !args.isNull() && !args.isObject())
    {
        *protocolError = QJsonObject{{QStringLiteral("code"), InvalidParams},
                                     {QStringLiteral("message"), QStringLiteral("arguments must be an object")}};
        return QJsonObject();
    }

    // A handler that spins an event loop (e.g. a modal dialog) could let a second request in; refuse it.
    if (m_handling)
    {
        *protocolError = QJsonObject{{QStringLiteral("code"), ServerBusy},
                                     {QStringLiteral("message"),
                                      QStringLiteral("Seamly2D is busy with another request or an open dialog.")}};
        return QJsonObject();
    }
    m_handling = true;
    const McpToolResult result = m_registry->call(name, args.toObject());
    m_handling = false;
    emit toolCalled(name, result.isError);

    QString text = result.text;
    if (!result.structured.isNull() && !result.structured.isUndefined())
    {
        const QByteArray json = result.structured.isArray()
                                    ? QJsonDocument(result.structured.toArray()).toJson(QJsonDocument::Compact)
                                    : QJsonDocument(result.structured.toObject()).toJson(QJsonDocument::Compact);
        text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + QString::fromUtf8(json);
    }
    QJsonObject out{{QStringLiteral("content"),
                     QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                            {QStringLiteral("text"), text}}}},
                    {QStringLiteral("isError"), result.isError}};
    if (result.structured.isObject())
    {
        out.insert(QStringLiteral("structuredContent"), result.structured.toObject());
    }
    return out;
}
