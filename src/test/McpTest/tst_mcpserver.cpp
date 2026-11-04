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
#include "tst_mcpserver.h"

#include <QEventLoop>
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonArray>
#include <QtTest>

#include "mcptestclient.h"

namespace
{
QJsonObject object(std::initializer_list<QPair<QString, QJsonValue>> items)
{
    QJsonObject result;
    for (const auto &item : items)
    {
        result.insert(item.first, item.second);
    }
    return result;
}

/// One tiny valid PNG (1x1), enough to check that image bytes survive base64.
const QByteArray kPng = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");
} // namespace

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::initTestCase()
{
    McpTool echo;
    echo.name = QStringLiteral("echo");
    echo.description = QStringLiteral("Returns its argument.");
    echo.inputSchema = object({{QStringLiteral("type"), QStringLiteral("object")},
                               {QStringLiteral("properties"),
                                object({{QStringLiteral("text"), object({{QStringLiteral("type"),
                                                                           QStringLiteral("string")}})}})}});
    echo.readOnly = true;
    echo.handler = [](const QJsonObject &arguments)
    {
        return McpToolResult::ok(QStringLiteral("echo: %1").arg(arguments.value(QStringLiteral("text")).toString()),
                                 object({{QStringLiteral("text"), arguments.value(QStringLiteral("text"))}}));
    };
    m_registry.registerTool(echo);

    McpTool fail;
    fail.name = QStringLiteral("fail");
    fail.description = QStringLiteral("Always reports an error.");
    fail.inputSchema = object({{QStringLiteral("type"), QStringLiteral("object")}});
    fail.handler = [](const QJsonObject &) { return McpToolResult::error(QStringLiteral("this did not work")); };
    m_registry.registerTool(fail);

    McpTool image;
    image.name = QStringLiteral("image");
    image.description = QStringLiteral("Returns a picture.");
    image.inputSchema = object({{QStringLiteral("type"), QStringLiteral("object")}});
    image.handler = [](const QJsonObject &)
    {
        McpToolResult result = McpToolResult::ok(QStringLiteral("a picture"));
        result.imagePng = kPng;
        return result;
    };
    m_registry.registerTool(image);

    McpTool slow;   // spins an event loop like a tool that waits for a dialog
    slow.name = QStringLiteral("slow");
    slow.description = QStringLiteral("Waits a moment.");
    slow.inputSchema = object({{QStringLiteral("type"), QStringLiteral("object")}});
    slow.handler = [](const QJsonObject &)
    {
        QEventLoop loop;
        QTimer::singleShot(600, &loop, &QEventLoop::quit);
        loop.exec();
        return McpToolResult::ok(QStringLiteral("done waiting"));
    };
    m_registry.registerTool(slow);

    m_server = new McpServer(&m_registry, this);
    m_server->setServerInfo(QStringLiteral("test-server"), QStringLiteral("Test Server"), QStringLiteral("1.2.3"));
    m_server->setInstructions(QStringLiteral("Use echo."));
    m_token = QStringLiteral("test-token");
    m_server->setToken(m_token);
    QVERIFY2(m_server->start(0), qPrintable(m_server->errorString()));
    QVERIFY(m_server->port() != 0);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::cleanupTestCase()
{
    m_server->stop();
    QVERIFY(!m_server->isListening());
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject TST_McpServer::call(const QString &method, const QJsonObject &params, int id)
{
    QJsonObject message = object({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                  {QStringLiteral("id"), id},
                                  {QStringLiteral("method"), method}});
    if (!params.isEmpty())
    {
        message.insert(QStringLiteral("params"), params);
    }
    return m_server->handleMessage(message).toObject();
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::initializeNegotiatesTheVersion()
{
    // A version the server knows is answered with itself ...
    QJsonObject reply = call(QStringLiteral("initialize"),
                             object({{QStringLiteral("protocolVersion"), QStringLiteral("2025-03-26")},
                                     {QStringLiteral("capabilities"), QJsonObject()},
                                     {QStringLiteral("clientInfo"),
                                      object({{QStringLiteral("name"), QStringLiteral("t")},
                                              {QStringLiteral("version"), QStringLiteral("1")}})}}));
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("protocolVersion")).toString(),
             QStringLiteral("2025-03-26"));

    // ... an unknown one with the newest version the server speaks.
    reply = call(QStringLiteral("initialize"),
                 object({{QStringLiteral("protocolVersion"), QStringLiteral("1999-01-01")},
                         {QStringLiteral("capabilities"), QJsonObject()},
                         {QStringLiteral("clientInfo"), object({{QStringLiteral("name"), QStringLiteral("t")},
                                                                {QStringLiteral("version"), QStringLiteral("1")}})}}));
    QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("protocolVersion")).toString(),
             McpServer::LatestProtocolVersion);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::initializeReportsInstructionsAndTools()
{
    QSignalSpy spy(m_server, &McpServer::clientInitialized);
    const QJsonObject result =
        call(QStringLiteral("initialize"),
             object({{QStringLiteral("protocolVersion"), McpServer::LatestProtocolVersion},
                     {QStringLiteral("capabilities"), QJsonObject()},
                     {QStringLiteral("clientInfo"), object({{QStringLiteral("name"), QStringLiteral("my-client")},
                                                            {QStringLiteral("version"), QStringLiteral("9")}})}}))
            .value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("instructions")).toString(), QStringLiteral("Use echo."));
    QCOMPARE(result.value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("test-server"));
    QVERIFY(result.value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("tools")));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("my-client"));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::toolsListDescribesTheTools()
{
    const QJsonArray tools = call(QStringLiteral("tools/list")).value(QStringLiteral("result")).toObject()
                                 .value(QStringLiteral("tools")).toArray();
    QStringList names;
    for (const QJsonValue &tool : tools)
    {
        const QJsonObject t = tool.toObject();
        names << t.value(QStringLiteral("name")).toString();
        QVERIFY2(!t.value(QStringLiteral("description")).toString().isEmpty(), qPrintable(names.last()));
        QCOMPARE(t.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("object"));
        if (names.last() == QLatin1String("echo"))
        {
            QVERIFY(t.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("readOnlyHint")).toBool());
        }
    }
    names.sort();
    QCOMPARE(names, (QStringList{QStringLiteral("echo"), QStringLiteral("fail"), QStringLiteral("image"),
                                 QStringLiteral("slow")}));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::toolCallReturnsTextAndStructuredContent()
{
    const QJsonObject result =
        call(QStringLiteral("tools/call"),
             object({{QStringLiteral("name"), QStringLiteral("echo")},
                     {QStringLiteral("arguments"), object({{QStringLiteral("text"), QStringLiteral("hi")}})}}))
            .value(QStringLiteral("result")).toObject();
    QVERIFY(!result.value(QStringLiteral("isError")).toBool());
    const QString text = result.value(QStringLiteral("content")).toArray().at(0).toObject()
                             .value(QStringLiteral("text")).toString();
    QVERIFY2(text.startsWith(QStringLiteral("echo: hi")), qPrintable(text));
    QCOMPARE(result.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("hi"));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::toolErrorIsAResultNotAProtocolError()
{
    const QJsonObject reply = call(QStringLiteral("tools/call"), object({{QStringLiteral("name"),
                                                                           QStringLiteral("fail")}}));
    QVERIFY(!reply.contains(QStringLiteral("error")));   // the model must be able to read and correct the call
    const QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QVERIFY(result.value(QStringLiteral("isError")).toBool());
    QCOMPARE(result.value(QStringLiteral("content")).toArray().at(0).toObject().value(QStringLiteral("text"))
                 .toString(),
             QStringLiteral("this did not work"));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::toolCallProblemsAreProtocolErrors()
{
    QVERIFY(call(QStringLiteral("tools/call"), object({{QStringLiteral("name"), QStringLiteral("nope")}}))
                .contains(QStringLiteral("error")));
    QVERIFY(call(QStringLiteral("tools/call"), QJsonObject()).contains(QStringLiteral("error")));
    QVERIFY(call(QStringLiteral("tools/call"),
                 object({{QStringLiteral("name"), QStringLiteral("echo")},
                         {QStringLiteral("arguments"), QStringLiteral("not an object")}}))
                .contains(QStringLiteral("error")));
    QVERIFY(call(QStringLiteral("no/such/method")).contains(QStringLiteral("error")));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::imageResultIsReturnedAsImageContent()
{
    const QJsonArray content = call(QStringLiteral("tools/call"), object({{QStringLiteral("name"),
                                                                            QStringLiteral("image")}}))
                                   .value(QStringLiteral("result")).toObject().value(QStringLiteral("content"))
                                   .toArray();
    QCOMPARE(content.size(), 2);
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    const QJsonObject image = content.at(1).toObject();
    QCOMPARE(image.value(QStringLiteral("type")).toString(), QStringLiteral("image"));
    QCOMPARE(image.value(QStringLiteral("mimeType")).toString(), QStringLiteral("image/png"));
    QCOMPARE(QByteArray::fromBase64(image.value(QStringLiteral("data")).toString().toLatin1()), kPng);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::notificationsGetNoReply()
{
    const QJsonValue reply = m_server->handleMessage(
        object({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("method"), QStringLiteral("notifications/initialized")}}));
    QVERIFY(reply.isNull() || reply.isUndefined());
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::pingAnswers()
{
    const QJsonObject reply = call(QStringLiteral("ping"), QJsonObject(), 7);
    QCOMPARE(reply.value(QStringLiteral("id")).toInt(), 7);
    QVERIFY(reply.contains(QStringLiteral("result")));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::wrongJsonRpcVersionIsRejected()
{
    const QJsonValue reply = m_server->handleMessage(object({{QStringLiteral("jsonrpc"), QStringLiteral("1.0")},
                                                             {QStringLiteral("id"), 1},
                                                             {QStringLiteral("method"), QStringLiteral("ping")}}));
    QVERIFY(reply.toObject().contains(QStringLiteral("error")));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::rejectsMissingOrWrongToken()
{
    const QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    QCOMPARE(httpRequest(m_server->port(), "POST", "/mcp", {}, body).status, 401);
    QCOMPARE(httpRequest(m_server->port(), "POST", "/mcp", {{"Authorization", "Bearer wrong"}}, body).status, 401);
    QCOMPARE(httpRequest(m_server->port(), "POST", "/mcp", {{"Authorization", "Basic dGVzdDp0ZXN0"}}, body).status,
             401);
    QCOMPARE(httpRequest(m_server->port(), "POST", "/mcp", {{"Authorization", "Bearer test-token-and-more"}}, body)
                 .status,
             401);
    QCOMPARE(rpc(m_server->port(), m_token, QJsonDocument::fromJson(body).object()).status, 200);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::acceptsTheTokenCaseInsensitivelyInTheScheme()
{
    const QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    QCOMPARE(httpRequest(m_server->port(), "POST", "/mcp", {{"Authorization", "bearer test-token"}}, body).status, 200);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::rejectsForeignHostAndOrigin()
{
    const QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    const Headers auth{{"Authorization", "Bearer test-token"}};
    // DNS rebinding: the browser sends the attacker's host name.
    Headers foreignHost = auth;
    foreignHost << qMakePair(QByteArray("Host"), QByteArray("evil.example.com"));
    QCOMPARE(httpRequest(m_server->port(), "POST", "/mcp", foreignHost, body).status, 403);

    Headers foreignOrigin = auth;
    foreignOrigin << qMakePair(QByteArray("Origin"), QByteArray("https://evil.example.com"));
    QCOMPARE(httpRequest(m_server->port(), "POST", "/mcp", foreignOrigin, body).status, 403);

    Headers localOrigin = auth;
    localOrigin << qMakePair(QByteArray("Origin"), QByteArray("http://localhost:3000"));
    QCOMPARE(httpRequest(m_server->port(), "POST", "/mcp", localOrigin, body).status, 200);

    Headers localhostName = auth;
    localhostName << qMakePair(QByteArray("Host"), QByteArray("localhost:") + QByteArray::number(m_server->port()));
    QCOMPARE(httpRequest(m_server->port(), "POST", "/mcp", localhostName, body).status, 200);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::onlyPostToTheEndpointIsAllowed()
{
    const Headers auth{{"Authorization", "Bearer test-token"}};
    QCOMPARE(httpRequest(m_server->port(), "GET", "/mcp", auth, QByteArray()).status, 405);
    QCOMPARE(httpRequest(m_server->port(), "DELETE", "/mcp", auth, QByteArray()).status, 405);
    QCOMPARE(httpRequest(m_server->port(), "POST", "/elsewhere", auth, QByteArray("{}")).status, 404);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::rejectsMalformedJson()
{
    const HttpReply reply = httpRequest(m_server->port(), "POST", "/mcp",
                                        {{"Authorization", "Bearer test-token"}}, QByteArray("{not json"));
    QCOMPARE(reply.status, 400);
    QCOMPARE(reply.json().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32700);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::rejectsOversizedBodies()
{
    // The limit is checked from the Content-Length header, before any body is read.
    const HttpReply reply = httpRequest(m_server->port(), "POST", "/mcp", {{"Authorization", "Bearer test-token"}},
                                        QByteArray("{}"), McpServer::MaxBodyBytes + 1);
    QCOMPARE(reply.status, 413);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::notificationsOverHttpAreAccepted()
{
    const HttpReply reply = rpc(m_server->port(), m_token,
                                object({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                        {QStringLiteral("method"), QStringLiteral("notifications/initialized")}}));
    QCOMPARE(reply.status, 202);
    QVERIFY(reply.body.isEmpty());
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::batchesAreAnsweredOneByOne()
{
    QJsonArray batch;
    batch.append(object({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 1},
                         {QStringLiteral("method"), QStringLiteral("ping")}}));
    batch.append(object({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                         {QStringLiteral("method"), QStringLiteral("notifications/initialized")}}));
    batch.append(object({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 2},
                         {QStringLiteral("method"), QStringLiteral("tools/list")}}));
    const HttpReply reply = rpc(m_server->port(), m_token, batch);
    QCOMPARE(reply.status, 200);
    const QJsonArray replies = reply.jsonArray();
    QCOMPARE(replies.size(), 2);   // the notification gets no reply
    QCOMPARE(replies.at(0).toObject().value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(replies.at(1).toObject().value(QStringLiteral("id")).toInt(), 2);

    // An empty batch is an invalid request.
    QVERIFY(rpc(m_server->port(), m_token, QJsonArray()).json().contains(QStringLiteral("error")));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::aSecondRequestWhileAToolRunsIsRefused()
{
    const QJsonObject slow = object({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 1},
                                     {QStringLiteral("method"), QStringLiteral("tools/call")},
                                     {QStringLiteral("params"), object({{QStringLiteral("name"),
                                                                         QStringLiteral("slow")}})}});
    // The tool runs a nested event loop inside the server's slot. A timer that fires in that loop plays the second
    // client, so its requests are really handled while the tool is running.
    HttpReply second;
    HttpReply secondTool;
    QTimer::singleShot(150, this, [&]()
    {
        second = rpc(m_server->port(), m_token,
                     object({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 2},
                             {QStringLiteral("method"), QStringLiteral("ping")}}));
        secondTool = rpc(m_server->port(), m_token,
                         object({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 3},
                                 {QStringLiteral("method"), QStringLiteral("tools/call")},
                                 {QStringLiteral("params"), object({{QStringLiteral("name"),
                                                                     QStringLiteral("echo")}})}}));
    });

    HttpCall first(m_server->port());
    first.start(buildRequest(m_server->port(), "POST", "/mcp", {{"Authorization", "Bearer test-token"}},
                             QJsonDocument(slow).toJson(QJsonDocument::Compact)));

    const HttpReply firstReply = first.wait();
    QCOMPARE(firstReply.status, 200);
    QVERIFY(firstReply.body.contains("done waiting"));

    // A ping is not a tool call and is still answered; a second tool call is refused with a JSON-RPC error.
    QCOMPARE(second.status, 200);
    QVERIFY(second.json().contains(QStringLiteral("result")));
    QCOMPARE(secondTool.status, 200);
    QVERIFY2(secondTool.json().contains(QStringLiteral("error")), qPrintable(QString::fromUtf8(secondTool.body)));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::aRunningToolIsNotAbortedByTheIdleTimeout()
{
    // The idle timeout (15 s in the program) must not cut a request whose tool waits for the user. Here: 200 ms
    // timeout, a tool that needs 600 ms.
    m_server->setConnectionTimeout(200);
    const QJsonObject slow = object({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 1},
                                     {QStringLiteral("method"), QStringLiteral("tools/call")},
                                     {QStringLiteral("params"), object({{QStringLiteral("name"),
                                                                         QStringLiteral("slow")}})}});
    const HttpReply reply = httpRequest(m_server->port(), "POST", "/mcp", {{"Authorization", "Bearer test-token"}},
                                        QJsonDocument(slow).toJson(QJsonDocument::Compact));
    QCOMPARE(reply.status, 200);
    QVERIFY(reply.body.contains("done waiting"));

    // A client that stalls with half a request is still disconnected.
    QTcpSocket stalled;
    stalled.connectToHost(QHostAddress::LocalHost, m_server->port());
    // (the server lives in this thread, so the waiting has to run the event loop)
    QTRY_COMPARE_WITH_TIMEOUT(stalled.state(), QAbstractSocket::ConnectedState, 2000);
    stalled.write("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n");
    QTRY_COMPARE_WITH_TIMEOUT(stalled.state(), QAbstractSocket::UnconnectedState, 3000);
    m_server->setConnectionTimeout(McpServer::ConnectionTimeoutMs);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpServer::newTokenLocksOutTheOldOne()
{
    const QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    const QString fresh = m_server->regenerateToken();
    QVERIFY(!fresh.isEmpty());
    QVERIFY(fresh != m_token);
    QCOMPARE(httpRequest(m_server->port(), "POST", "/mcp", {{"Authorization", "Bearer test-token"}}, body).status, 401);
    QCOMPARE(httpRequest(m_server->port(), "POST", "/mcp", {{"Authorization", "Bearer " + fresh.toLatin1()}}, body)
                 .status,
             200);
    m_token = fresh;
}
