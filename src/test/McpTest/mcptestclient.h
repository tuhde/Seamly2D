/***************************************************************************
 **  @brief  Tests for the MCP server (Claude connection).
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

#ifndef MCPTESTCLIENT_H
#define MCPTESTCLIENT_H

#include <QByteArray>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>

/// Result of one HTTP exchange with the MCP endpoint.
struct HttpReply
{
    int         status = 0;      ///< 0 if no (complete) response arrived
    QByteArray  body;

    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
    QJsonArray  jsonArray() const { return QJsonDocument::fromJson(body).array(); }
};

/// A non-blocking HTTP exchange (the event loop keeps running, so a server in the same thread can answer).
class HttpCall : public QObject
{
public:
    explicit HttpCall(quint16 port, QObject *parent = nullptr) : QObject(parent), m_port(port) {}

    /// Sends the raw bytes and collects the answer until the server closes the connection.
    void start(const QByteArray &raw)
    {
        m_data.clear();
        m_finished = false;
        connect(&m_socket, &QTcpSocket::readyRead, this, [this]() { m_data += m_socket.readAll(); });
        connect(&m_socket, &QTcpSocket::disconnected, this, [this]() { m_finished = true; emit_finished(); });
        connect(&m_socket, &QTcpSocket::connected, this, [this, raw]() { m_socket.write(raw); });
        m_socket.connectToHost(QHostAddress::LocalHost, m_port);
    }

    HttpReply wait(int timeoutMs = 20000)
    {
        QEventLoop loop;
        m_loop = &loop;
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
        if (!m_finished)
        {
            loop.exec();
        }
        m_loop = nullptr;
        return parse();
    }

private:
    void emit_finished() { if (m_loop != nullptr) { m_loop->quit(); } }

    HttpReply parse() const
    {
        HttpReply reply;
        const int lineEnd = m_data.indexOf("\r\n");
        if (lineEnd < 0)
        {
            return reply;
        }
        const QList<QByteArray> status = m_data.left(lineEnd).split(' ');
        reply.status = status.value(1).toInt();
        const int bodyStart = m_data.indexOf("\r\n\r\n");
        reply.body = bodyStart < 0 ? QByteArray() : m_data.mid(bodyStart + 4);
        return reply;
    }

    quint16      m_port;
    QTcpSocket   m_socket;
    QByteArray   m_data;
    bool         m_finished = false;
    QEventLoop  *m_loop = nullptr;
};

using Headers = QList<QPair<QByteArray, QByteArray>>;

/// Builds an HTTP request. A "Host" header in `headers` replaces the default loopback Host.
inline QByteArray buildRequest(quint16 port, const QByteArray &method, const QByteArray &path, Headers headers,
                               const QByteArray &body, qint64 declaredLength = -1)
{
    bool hasHost = false;
    for (const auto &h : headers)
    {
        hasHost = hasHost || h.first.toLower() == "host";
    }
    QByteArray request = method + ' ' + path + " HTTP/1.1\r\n";
    if (!hasHost)
    {
        request += "Host: 127.0.0.1:" + QByteArray::number(port) + "\r\n";
    }
    for (const auto &h : headers)
    {
        request += h.first + ": " + h.second + "\r\n";
    }
    request += "Content-Length: " + QByteArray::number(declaredLength >= 0 ? declaredLength : body.size()) + "\r\n";
    request += "Connection: close\r\n\r\n" + body;
    return request;
}

/// Sends one request and waits for the answer.
inline HttpReply httpRequest(quint16 port, const QByteArray &method, const QByteArray &path, const Headers &headers,
                             const QByteArray &body, qint64 declaredLength = -1, int timeoutMs = 20000)
{
    HttpCall call(port);
    call.start(buildRequest(port, method, path, headers, body, declaredLength));
    return call.wait(timeoutMs);
}

/// POSTs a JSON-RPC message to /mcp with the bearer token.
inline HttpReply rpc(quint16 port, const QString &token, const QJsonValue &message, int timeoutMs = 20000)
{
    const QByteArray body = message.isArray() ? QJsonDocument(message.toArray()).toJson(QJsonDocument::Compact)
                                              : QJsonDocument(message.toObject()).toJson(QJsonDocument::Compact);
    return httpRequest(port, "POST", "/mcp",
                       {{"Authorization", "Bearer " + token.toLatin1()}, {"Content-Type", "application/json"}},
                       body, -1, timeoutMs);
}

#endif // MCPTESTCLIENT_H
