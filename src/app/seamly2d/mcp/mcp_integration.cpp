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
#include <QJsonObject>

#include "../mainwindow.h"
#include "pattern_editor.h"
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
    "result. Save the work with save_pattern and write printable/cuttable files with export_layout; Seamly2D asks the user to confirm writing to a file or folder.";
}

//---------------------------------------------------------------------------------------------------------------------
McpIntegration::McpIntegration(MainWindow *window)
    : QObject(window)
    , m_window(window)
    , m_server(new McpServer(&m_registry, this))
{
    m_server->setServerInfo(QStringLiteral("seamly2d"), QStringLiteral("Seamly2D"),
                            QCoreApplication::applicationVersion());
    m_server->setInstructions(QString::fromUtf8(kInstructions));
    registerTools();

    connect(m_server, &McpServer::listeningChanged, this, [this](bool) { emit stateChanged(); });
    connect(m_server, &McpServer::clientInitialized, this, [this](const QString &name, const QString &)
    {
        m_clientName = name;
        emit stateChanged();
    });
}

//---------------------------------------------------------------------------------------------------------------------
McpIntegration::~McpIntegration() = default;

//---------------------------------------------------------------------------------------------------------------------
void McpIntegration::startFromSettings()
{
    // SEAMLY_MCP* are test hooks (integration tests / CI); users use Help > Claude connection.
    const bool forced = qEnvironmentVariableIsSet("SEAMLY_MCP");
    if (!forced && !qApp->Seamly2DSettings()->GetMcpEnabled())
    {
        return;
    }
    if (qEnvironmentVariableIsSet("SEAMLY_MCP_TOKEN"))
    {
        m_server->setToken(qEnvironmentVariable("SEAMLY_MCP_TOKEN"));
    }
    quint16 port = qApp->Seamly2DSettings()->GetMcpPort();
    if (qEnvironmentVariableIsSet("SEAMLY_MCP_PORT"))
    {
        port = static_cast<quint16>(qEnvironmentVariableIntValue("SEAMLY_MCP_PORT"));
    }
    start(port);
}

//---------------------------------------------------------------------------------------------------------------------
bool McpIntegration::start(quint16 port)
{
    if (m_server->start(port))
    {
        return true;
    }
    // The preferred port is taken (e.g. a second Seamly2D window): fall back to any free port.
    return port != 0 && m_server->start(0);
}

//---------------------------------------------------------------------------------------------------------------------
void McpIntegration::stop()
{
    m_server->stop();
    m_clientName.clear();
    emit stateChanged();
}

//---------------------------------------------------------------------------------------------------------------------
bool McpIntegration::isRunning() const
{
    return m_server->isListening();
}

//---------------------------------------------------------------------------------------------------------------------
quint16 McpIntegration::port() const
{
    return m_server->port();
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::token() const
{
    return m_server->token();
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::regenerateToken()
{
    m_clientName.clear();
    const QString token = m_server->regenerateToken();
    emit stateChanged();
    return token;
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::lastError() const
{
    return m_server->errorString();
}

//---------------------------------------------------------------------------------------------------------------------
QString McpIntegration::clientName() const
{
    return m_clientName;
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
    if (!m_clientName.isEmpty())
    {
        return tr("Claude connected (%1)").arg(m_clientName);
    }
    return tr("Claude: listening on port %1").arg(port());
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
    return McpToolResult::ok(QStringLiteral("Pattern info:"), m_window->mcpPatternInfo());
}
