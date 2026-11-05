/***************************************************************************
 **  @file   mcprouter.cpp
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

#include "mcprouter.h"

#include <QByteArray>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace
{
const QString kPatternArgument = QStringLiteral("pattern");
const QString kListPatterns    = QStringLiteral("list_patterns");

//---------------------------------------------------------------------------------------------------------------------
QString label(const McpInstance &instance)
{
    if (!instance.patternFile.isEmpty())
    {
        return QFileInfo(instance.patternFile).fileName();
    }
    return instance.title.isEmpty() ? QStringLiteral("window %1").arg(instance.pid) : instance.title;
}

//---------------------------------------------------------------------------------------------------------------------
const McpInstance *newest(const QList<McpInstance> &list, qint64 preferPid)
{
    const McpInstance *best = nullptr;
    for (const McpInstance &instance : list)
    {
        if (best == nullptr || instance.lastActivated > best->lastActivated
            || (instance.lastActivated == best->lastActivated && instance.pid == preferPid))
        {
            best = &instance;
        }
    }
    return best;
}
}  // namespace

//---------------------------------------------------------------------------------------------------------------------
McpRouter::McpRouter(const McpToolRegistry *local, const McpInstanceRegistry *instances,
                     std::function<QString()> token)
    : m_local(local)
    , m_instances(instances)
    , m_token(std::move(token))
{
}

//---------------------------------------------------------------------------------------------------------------------
void McpRouter::populate(McpToolRegistry *routed) const
{
    const QVector<McpTool> tools = m_local->tools();
    for (const McpTool &tool : tools)
    {
        McpTool wrapper = tool;
        QJsonObject schema = wrapper.inputSchema;
        if (schema.isEmpty())
        {
            schema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
        }
        QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
        properties.insert(kPatternArgument,
                          QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                      {QStringLiteral("description"),
                                       QStringLiteral("Which open Seamly2D window to use: its id, file name or "
                                                      "title from list_patterns. Optional; without it the window "
                                                      "the user used last is taken.")}});
        schema.insert(QStringLiteral("properties"), properties);
        wrapper.inputSchema = schema;
        const QString name = tool.name;
        wrapper.handler = [this, name](const QJsonObject &arguments) { return route(name, arguments); };
        routed->registerTool(wrapper);
    }

    McpTool list;
    list.name = kListPatterns;
    list.description = QStringLiteral("Lists the Seamly2D windows that are open: id, file, title, unsaved changes, "
                                      "and which one was used last. Every other tool takes an optional `pattern` "
                                      "(id, file name or title) to choose the window; call this first when more "
                                      "than one window is open.");
    list.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject()}};
    list.readOnly = true;
    list.handler = [this](const QJsonObject &) { return listPatterns(); };
    routed->registerTool(list);
}

//---------------------------------------------------------------------------------------------------------------------
McpRouter::Target McpRouter::resolve(const QString &pattern) const
{
    Target target;
    const QList<McpInstance> all = m_instances->instances();
    if (all.isEmpty())
    {
        target.error = QStringLiteral("No Seamly2D window is available.");
        return target;
    }

    if (pattern.trimmed().isEmpty())
    {
        target.instance = *newest(all, m_instances->pid());
        target.ok = true;
        return target;
    }

    const QString wanted = pattern.trimmed();
    QList<McpInstance> matches;
    for (const McpInstance &instance : all)
    {
        if (QString::number(instance.pid) == wanted)
        {
            matches = {instance};
            break;
        }
        if ((!instance.patternFile.isEmpty() && instance.patternFile == wanted)
            || label(instance).compare(wanted, Qt::CaseInsensitive) == 0
            || instance.title.compare(wanted, Qt::CaseInsensitive) == 0)
        {
            matches.append(instance);
        }
    }
    if (matches.size() == 1)
    {
        target.instance = matches.first();
        target.ok = true;
    }
    else
    {
        target.error = matches.isEmpty()
            ? QStringLiteral("No open window matches \"%1\". Call list_patterns to see the open windows.").arg(wanted)
            : QStringLiteral("\"%1\" matches %2 windows. Use the id from list_patterns.").arg(wanted).arg(matches.size());
    }
    return target;
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult McpRouter::route(const QString &tool, QJsonObject arguments) const
{
    const QString pattern = arguments.take(kPatternArgument).toString();
    const Target target = resolve(pattern);
    if (!target.ok)
    {
        return McpToolResult::error(target.error);
    }

    const bool local = target.instance.pid == m_instances->pid();
    McpToolResult result;
    if (local)
    {
        result = m_local->call(tool, arguments);
    }
    else
    {
        result = callRemote(target.instance.port, m_token(), tool, arguments, m_remoteTimeoutMs);
    }
    if (m_logger)
    {
        m_logger(tool, target.instance, result.isError, local);
    }

    // With several windows open, always say which one the answer is about.
    if (m_instances->instances().size() > 1)
    {
        result.text = QStringLiteral("[window %1: %2]\n").arg(target.instance.pid).arg(label(target.instance))
                      + result.text;
    }
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject McpRouter::describe(const McpInstance &instance, bool self, bool newest)
{
    return QJsonObject{{QStringLiteral("id"), static_cast<double>(instance.pid)},
                       {QStringLiteral("file"), instance.patternFile},
                       {QStringLiteral("title"), instance.title},
                       {QStringLiteral("modified"), instance.modified},
                       {QStringLiteral("lastUsed"), newest},
                       {QStringLiteral("hub"), self}};
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult McpRouter::listPatterns() const
{
    const QList<McpInstance> all = m_instances->instances();
    const McpInstance *last = all.isEmpty() ? nullptr : newest(all, m_instances->pid());
    QJsonArray array;
    for (const McpInstance &instance : all)
    {
        array.append(describe(instance, instance.pid == m_instances->pid(), last != nullptr && last->pid == instance.pid));
    }
    return McpToolResult::ok(QStringLiteral("%1 open Seamly2D window(s). Pass `pattern` (id, file name or title) to a "
                                            "tool to choose one; without it the window with lastUsed is taken.")
                                 .arg(all.size()),
                             array);
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult McpRouter::callRemote(quint16 port, const QString &token, const QString &tool,
                                    const QJsonObject &arguments, int timeoutMs)
{
    const QJsonObject body{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                           {QStringLiteral("id"), 1},
                           {QStringLiteral("method"), QStringLiteral("tools/call")},
                           {QStringLiteral("params"), QJsonObject{{QStringLiteral("name"), tool},
                                                                  {QStringLiteral("arguments"), arguments}}}};

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toLatin1());
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (!reply->isFinished())
    {
        reply->abort();
        reply->deleteLater();
        return McpToolResult::error(QStringLiteral("The other Seamly2D window did not answer in time."));
    }
    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkText = reply->errorString();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError && payload.isEmpty())
    {
        return McpToolResult::error(QStringLiteral("The other Seamly2D window is not reachable (%1).").arg(networkText));
    }

    const QJsonObject json = QJsonDocument::fromJson(payload).object();
    if (json.contains(QStringLiteral("error")))
    {
        return McpToolResult::error(json.value(QStringLiteral("error")).toObject()
                                        .value(QStringLiteral("message")).toString(QStringLiteral("Request failed.")));
    }
    const QJsonObject result = json.value(QStringLiteral("result")).toObject();
    McpToolResult out;
    out.isError = result.value(QStringLiteral("isError")).toBool();
    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    for (const QJsonValue &item : content)
    {
        const QJsonObject block = item.toObject();
        if (block.value(QStringLiteral("type")).toString() == QLatin1String("text"))
        {
            out.text += (out.text.isEmpty() ? QString() : QStringLiteral("\n")) + block.value(QStringLiteral("text")).toString();
        }
        else if (block.value(QStringLiteral("type")).toString() == QLatin1String("image"))
        {
            out.imagePng = QByteArray::fromBase64(block.value(QStringLiteral("data")).toString().toLatin1());
        }
    }
    // The other window's server appended the structured payload to the text; keep it structured, as it was there.
    const QJsonValue structured = result.value(QStringLiteral("structuredContent"));
    if (structured.isObject())
    {
        const QString json = QString::fromUtf8(QJsonDocument(structured.toObject()).toJson(QJsonDocument::Compact));
        if (out.text.endsWith(json))
        {
            out.text.chop(json.size());
            if (out.text.endsWith(QLatin1Char('\n')))
            {
                out.text.chop(1);
            }
        }
        out.structured = structured;
    }
    return out;
}
