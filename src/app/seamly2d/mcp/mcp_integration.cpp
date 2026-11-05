/***************************************************************************
 **  @file   mcp_integration.cpp
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

#include "mcp_integration.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <QJsonObject>

#include "../mainwindow.h"
#include "pattern_editor.h"
#include "../../libs/vmcp/mcprouter.h"
#include "../../libs/vmcp/mcpserver.h"
#include "../core/application_2d.h"
#include "../../libs/vmisc/vsettings.h"

namespace
{
const char *const kInstructions =
    "Seamly2D is a pattern-making application. This server controls the pattern that is open in the running "
    "Seamly2D window. Every change appears on the canvas at once and can be undone with Undo.\n"
    "Work like this: first read the current state (get_pattern_info, list_objects). If the pattern has no draft block "
    "yet, create one with add_draft_block (it also sets the unit). To draft from someone's measurements, create a "
    "measurement file (create_measurements, names from list_standard_measurements) and attach it "
    "(attach_measurements), then use the measurement names in formulas. Then change the pattern (add_point, add_line, add_curve, add_arc) and "
    "check the result with list_objects. Points refer to earlier points by their id. A drawing becomes a pattern "
    "piece (what gets laid out and exported) with add_piece, which takes the outline's point ids in order; add the details a cutter needs with set_grainline, set_notch and update_piece (quantity, fold, label texts). Call "
    "fit_view after drawing, use screenshot to look at the result yourself, and show_mode to switch between the draft and piece view, so that the user sees the "
    "result. Save the work with save_pattern and write printable/cuttable files with export_layout; Seamly2D asks the user to confirm writing to a file or folder.\n"
    "Several Seamly2D windows can be open, each with its own pattern. list_patterns shows them; every tool accepts an "
    "optional `pattern` (id, file name or title) to choose the window. Without it the window the user used last is "
    "taken, so name the pattern when more than one is open.";
}

//---------------------------------------------------------------------------------------------------------------------
McpIntegration::McpIntegration(MainWindow *window)
    : QObject(window)
    , m_window(window)
    , m_server(new McpServer(&m_registry, this))
    , m_hubServer(new McpServer(&m_hubRegistry, this))
{
    for (McpServer *server : {m_server, m_hubServer})
    {
        server->setServerInfo(QStringLiteral("seamly2d"), QStringLiteral("Seamly2D"),
                              QCoreApplication::applicationVersion());
        server->setInstructions(QString::fromUtf8(kInstructions));
    }
    registerTools();

    m_router.reset(new McpRouter(&m_registry, &m_instances, [this]() { return token(); }));
    m_router->setLogger([this](const QString &tool, const McpInstance &target, bool isError, bool local)
    {
        const QString result = isError ? tr("error") : tr("ok");
        if (local)
        {
            log(QStringLiteral("%1: %2").arg(tool, result));
        }
        else
        {
            const QString name = target.patternFile.isEmpty() ? target.title : QFileInfo(target.patternFile).fileName();
            log(tr("%1 -> window %2 (%3): %4").arg(tool).arg(target.pid).arg(name, result));
        }
    });
    m_router->populate(&m_hubRegistry);

    connect(m_server, &McpServer::listeningChanged, this, [this](bool) { emit stateChanged(); });
    connect(m_hubServer, &McpServer::listeningChanged, this, [this](bool) { emit stateChanged(); });
    connect(m_hubServer, &McpServer::clientInitialized, this, [this](const QString &name, const QString &)
    {
        m_clientName = name;
        log(tr("Client connected: %1").arg(name));
        emit stateChanged();
    });
    connect(m_hubServer, &McpServer::requestReceived, this, [this](const QString &)
    {
        m_lastRequest = QDateTime::currentDateTime();
        if (!m_wasActive)
        {
            m_wasActive = true;
            emit stateChanged();
        }
    });
    // Calls that another window (the hub) routed to this one.
    connect(m_server, &McpServer::toolCalled, this, [this](const QString &tool, bool isError)
    {
        log(QStringLiteral("%1: %2").arg(tool, isError ? tr("error") : tr("ok")));
    });

    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &McpIntegration::tick);
    window->installEventFilter(this);
}

//---------------------------------------------------------------------------------------------------------------------
McpIntegration::~McpIntegration()
{
    m_instances.remove();
}

//---------------------------------------------------------------------------------------------------------------------
bool McpIntegration::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::WindowActivate)
    {
        m_lastActivated = QDateTime::currentMSecsSinceEpoch();
        if (m_running)
        {
            publishWindow();
        }
    }
    return QObject::eventFilter(watched, event);
}

//---------------------------------------------------------------------------------------------------------------------
void McpIntegration::log(const QString &message)
{
    emit logMessage(message);
}

//---------------------------------------------------------------------------------------------------------------------
void McpIntegration::startFromSettings()
{
    // SEAMLY_MCP* are test hooks (integration tests / CI); users use the "MCP Config and Log" panel.
    const bool forced = qEnvironmentVariableIsSet("SEAMLY_MCP");
    if (!forced && !qApp->Seamly2DSettings()->GetMcpEnabled())
    {
        return;
    }
    quint16 port = qApp->Seamly2DSettings()->GetMcpPort();
    if (qEnvironmentVariableIsSet("SEAMLY_MCP_PORT"))
    {
        port = static_cast<quint16>(qEnvironmentVariableIntValue("SEAMLY_MCP_PORT"));
    }
    start(port);
}

//---------------------------------------------------------------------------------------------------------------------
void McpIntegration::ensureToken()
{
    QString token;
    if (qEnvironmentVariableIsSet("SEAMLY_MCP_TOKEN"))
    {
        m_tokenFromEnvironment = true;   // test hook: neither saved nor replaced
        token = qEnvironmentVariable("SEAMLY_MCP_TOKEN");
    }
    else
    {
        VSettings *settings = qApp->Seamly2DSettings();
        settings->sync();
        token = settings->GetMcpToken();
        if (token.isEmpty())
        {
            token = m_server->regenerateToken();
            settings->SetMcpToken(token);   // kept, so that the client configuration stays valid
        }
    }
    m_server->setToken(token);
    m_hubServer->setToken(token);
}

//---------------------------------------------------------------------------------------------------------------------
void McpIntegration::adoptSavedToken()
{
    if (m_tokenFromEnvironment)
    {
        return;
    }
    VSettings *settings = qApp->Seamly2DSettings();
    settings->sync();   // another window may have created a new token
    const QString saved = settings->GetMcpToken();
    if (!saved.isEmpty() && saved != token())
    {
        m_server->setToken(saved);
        m_hubServer->setToken(saved);
        log(tr("The token was changed in another window."));
        emit stateChanged();
    }
}

//---------------------------------------------------------------------------------------------------------------------
bool McpIntegration::start(quint16 port)
{
    m_publicPort = port;
    ensureToken();
    if (!m_server->isListening() && !m_server->start(0))
    {
        log(tr("Could not start: %1").arg(m_server->errorString()));
        return false;
    }
    m_running = true;
    if (m_lastActivated == 0)
    {
        m_lastActivated = QDateTime::currentMSecsSinceEpoch();
    }
    publishWindow();
    tryBecomeHub();
    m_timer.start();
    emit stateChanged();
    return true;
}

//---------------------------------------------------------------------------------------------------------------------
void McpIntegration::tryBecomeHub()
{
    if (!m_running || m_hubServer->isListening())
    {
        return;
    }
    if (m_hubServer->start(m_publicPort))
    {
        m_loggedPortBusy = false;
        log(tr("Listening on 127.0.0.1:%1. This window answers Claude and passes calls on to the other windows.")
                .arg(m_hubServer->port()));
        publishWindow();
    }
    else if (!m_loggedPortBusy)
    {
        m_loggedPortBusy = true;
        log(tr("Port %1 is in use. Another Seamly2D window answers Claude; this window is reached through it. "
               "If no other window is open, another program uses the port.").arg(m_publicPort));
        emit stateChanged();
    }
}

//---------------------------------------------------------------------------------------------------------------------
void McpIntegration::publishWindow()
{
    const QJsonObject info = m_window->mcpPatternInfo();
    McpInstance self;
    self.port = m_server->port();
    self.patternFile = info.value(QStringLiteral("file")).toString();
    self.modified = info.value(QStringLiteral("modified")).toBool();
    self.title = m_window->windowTitle().remove(QStringLiteral("[*]")).trimmed();
    self.lastActivated = m_lastActivated;
    m_instances.publish(self);
}

//---------------------------------------------------------------------------------------------------------------------
void McpIntegration::tick()
{
    // "Connected" ends when the client stays quiet: the status must not stay on after the last Claude was closed.
    const bool active = clientActive();
    if (m_wasActive && !active)
    {
        m_wasActive = false;
        log(tr("No request from %1 for %2 seconds. It is probably closed.").arg(m_clientName).arg(ActiveSeconds));
        emit stateChanged();
    }
    else if (!active && m_lastRequest.isValid())
    {
        emit stateChanged();   // "last request ... ago" counts up
    }
    adoptSavedToken();
    publishWindow();
    tryBecomeHub();

    const QList<McpInstance> now = m_instances.instances();
    if (now != m_knownWindows)
    {
        m_knownWindows = now;
        emit stateChanged();
    }
}

//---------------------------------------------------------------------------------------------------------------------
void McpIntegration::stop()
{
    m_timer.stop();
    m_running = false;
    m_hubServer->stop();
    m_server->stop();
    m_instances.remove();
    m_knownWindows.clear();
    m_clientName.clear();
    m_loggedPortBusy = false;
    log(tr("Stopped."));
    emit stateChanged();
}

//---------------------------------------------------------------------------------------------------------------------
bool McpIntegration::isRunning() const
{
    return m_running && m_server->isListening();
}

//---------------------------------------------------------------------------------------------------------------------
bool McpIntegration::isHub() const
{
    return m_hubServer->isListening();
}

//---------------------------------------------------------------------------------------------------------------------
quint16 McpIntegration::port() const
{
    return m_hubServer->isListening() ? m_hubServer->port() : m_publicPort;
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::token() const
{
    return m_hubServer->token();
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::regenerateToken()
{
    m_clientName.clear();
    const QString token = m_server->regenerateToken();
    m_hubServer->setToken(token);
    if (!m_tokenFromEnvironment)
    {
        qApp->Seamly2DSettings()->SetMcpToken(token);
        qApp->Seamly2DSettings()->sync();
    }
    log(tr("New token created. Update the configuration of your Claude client."));
    emit stateChanged();
    return token;
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::lastError() const
{
    return m_hubServer->isListening() ? m_server->errorString() : m_hubServer->errorString();
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::clientName() const
{
    return m_clientName;
}

//---------------------------------------------------------------------------------------------------------------------
QList<McpInstance> McpIntegration::windows() const
{
    return m_instances.instances();
}

//---------------------------------------------------------------------------------------------------------------------
qint64 McpIntegration::windowId() const
{
    return m_instances.pid();
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::mcpJsonSnippet() const
{
    const QJsonObject server{{QStringLiteral("type"), QStringLiteral("http")},
                             {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:%1%2").arg(port()).arg(McpServer::Endpoint)},
                             {QStringLiteral("headers"),
                              QJsonObject{{QStringLiteral("Authorization"), QStringLiteral("Bearer ") + token()}}}};
    const QJsonObject root{{QStringLiteral("mcpServers"), QJsonObject{{QStringLiteral("seamly2d"), server}}}};
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::claudeCodeCommand() const
{
    return QStringLiteral("claude mcp add --transport http seamly2d http://127.0.0.1:%1%2 "
                          "--header \"Authorization: Bearer %3\"")
        .arg(port()).arg(McpServer::Endpoint, token());
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::statusText() const
{
    if (!isRunning())
    {
        return QString();
    }
    if (!isHub())
    {
        return tr("Claude: via another window (port %1)").arg(port());
    }
    if (clientActive())
    {
        return tr("Claude connected (%1)").arg(m_clientName);
    }
    return tr("Claude: listening on port %1").arg(port());
}

//---------------------------------------------------------------------------------------------------------------------
bool McpIntegration::clientActive() const
{
    return m_hubServer->isListening() && m_lastRequest.isValid() && m_lastRequest.secsTo(QDateTime::currentDateTime()) < ActiveSeconds;
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::clientText() const
{
    if (!m_lastRequest.isValid())
    {
        return tr("no client yet");
    }
    const QString name = m_clientName.isEmpty() ? tr("a client") : m_clientName;
    if (clientActive())
    {
        return tr("connected: %1").arg(name);
    }
    const qint64 seconds = m_lastRequest.secsTo(QDateTime::currentDateTime());
    const QString ago = seconds < 120 ? tr("%1 s").arg(seconds)
                      : seconds < 7200 ? tr("%1 min").arg(seconds / 60)
                                       : tr("%1 h").arg(seconds / 3600);
    return tr("idle, last request from %1 %2 ago").arg(name, ago);
}

//---------------------------------------------------------------------------------------------------------------------
void McpIntegration::registerTools()
{
    McpTool info;
    info.name = QStringLiteral("get_pattern_info");
    info.description =
        QStringLiteral("Returns the state of the pattern open in Seamly2D: unit, file path, whether it has unsaved "
                       "changes, the draft blocks (and which one is active), the measurement file and how many "
                       "geometric objects exist. Call this first to learn what you are working on.");
    info.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                   {QStringLiteral("properties"), QJsonObject()}};
    info.readOnly = true;
    info.handler = [this](const QJsonObject &arguments) { return getPatternInfo(arguments); };
    m_registry.registerTool(info);
    m_registry.registerTool(PatternEditor::addDraftBlockTool());
    m_registry.registerTool(PatternEditor::listObjectsTool());
    m_registry.registerTool(PatternEditor::addPointTool());
    m_registry.registerTool(PatternEditor::addLineTool());
    m_registry.registerTool(PatternEditor::addCurveTool());
    m_registry.registerTool(PatternEditor::addArcTool());
    m_registry.registerTool(PatternEditor::addAnchorPointTool());
    m_registry.registerTool(PatternEditor::addInternalPathTool());
    m_registry.registerTool(PatternEditor::setLabelTool());
    m_registry.registerTool(PatternEditor::setSizeTool());
    m_registry.registerTool(PatternEditor::setCornerTool());
    m_registry.registerTool(PatternEditor::addEllipticalArcTool());
    m_registry.registerTool(PatternEditor::listVariablesTool());
    m_registry.registerTool(PatternEditor::listStandardMeasurementsTool());
    m_registry.registerTool(PatternEditor::createMeasurementsTool());
    m_registry.registerTool(PatternEditor::attachMeasurementsTool());
    m_registry.registerTool(PatternEditor::setMeasurementTool());
    m_registry.registerTool(PatternEditor::undoTool());
    m_registry.registerTool(PatternEditor::redoTool());
    m_registry.registerTool(PatternEditor::setVariableTool());
    m_registry.registerTool(PatternEditor::deleteVariableTool());
    m_registry.registerTool(PatternEditor::updateObjectTool());
    m_registry.registerTool(PatternEditor::deleteObjectTool());
    m_registry.registerTool(PatternEditor::addPieceTool());
    m_registry.registerTool(PatternEditor::listPiecesTool());
    m_registry.registerTool(PatternEditor::updatePieceTool());
    m_registry.registerTool(PatternEditor::setGrainlineTool());
    m_registry.registerTool(PatternEditor::setNotchTool());
    m_registry.registerTool(PatternEditor::deletePieceTool());
    m_registry.registerTool(PatternEditor::showModeTool());
    m_registry.registerTool(PatternEditor::exportLayoutTool());
    m_registry.registerTool(PatternEditor::savePatternTool());
    m_registry.registerTool(PatternEditor::openPatternTool());
    m_registry.registerTool(PatternEditor::screenshotTool());
    m_registry.registerTool(PatternEditor::fitViewTool());
}

//---------------------------------------------------------------------------------------------------------------------
McpToolResult McpIntegration::getPatternInfo(const QJsonObject &arguments)
{
    Q_UNUSED(arguments)
    QJsonObject info = m_window->mcpPatternInfo();
    info.insert(QStringLiteral("windowId"), static_cast<double>(m_instances.pid()));   // for the `pattern` argument
    return McpToolResult::ok(QStringLiteral("Pattern info:"), info);
}
