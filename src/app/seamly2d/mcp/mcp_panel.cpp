/***************************************************************************
 **  @file   mcp_panel.cpp
 **
 **  @brief  Dock panel with the MCP configuration and a log of what Claude does.
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

#include "mcp_panel.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTime>
#include <QVBoxLayout>

#include "mcp_integration.h"
#include "../core/application_2d.h"
#include "../../libs/vmisc/vsettings.h"

//---------------------------------------------------------------------------------------------------------------------
McpPanel::McpPanel(McpIntegration *mcp, QWidget *parent)
    : QDockWidget(tr("MCP Config and Log"), parent)
    , m_mcp(mcp)
    , m_enabled(new QCheckBox(tr("Allow Claude to connect"), this))
    , m_port(new QSpinBox(this))
    , m_status(new QLabel(this))
    , m_windows(new QLabel(this))
    , m_token(new QLineEdit(this))
    , m_config(new QPlainTextEdit(this))
    , m_log(new QPlainTextEdit(this))
{
    setObjectName(QStringLiteral("mcp_DockWidget"));   // needed to save and restore the window layout

    auto *intro = new QLabel(tr("Claude can create and change patterns in the open Seamly2D windows. Changes appear "
                                "at once and can be undone. The connection stays on this computer. The token is "
                                "saved, so you configure your Claude client only once."), this);
    intro->setWordWrap(true);

    m_port->setRange(0, 65535);
    m_port->setValue(qApp->Seamly2DSettings()->GetMcpPort());
    m_port->setToolTip(tr("Port that Claude connects to. 0 picks a free port each time."));
    m_enabled->setChecked(m_mcp->isRunning() || qApp->Seamly2DSettings()->GetMcpEnabled());

    m_status->setWordWrap(true);
    m_windows->setWordWrap(true);
    m_windows->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_token->setReadOnly(true);
    auto *copyToken = new QPushButton(tr("Copy"), this);
    auto *newToken = new QPushButton(tr("New token"), this);
    newToken->setToolTip(tr("Replaces the saved token. Your Claude client has to be configured again."));
    auto *tokenRow = new QHBoxLayout();
    tokenRow->addWidget(m_token, 1);
    tokenRow->addWidget(copyToken);
    tokenRow->addWidget(newToken);

    m_config->setReadOnly(true);
    m_config->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_config->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_config->setMaximumHeight(130);
    auto *copyConfig = new QPushButton(tr("Copy .mcp.json"), this);
    auto *copyCommand = new QPushButton(tr("Copy Claude Code command"), this);
    auto *copyRow = new QHBoxLayout();
    copyRow->addWidget(copyConfig);
    copyRow->addWidget(copyCommand);
    copyRow->addStretch(1);

    auto *form = new QFormLayout();
    form->addRow(m_enabled);
    form->addRow(tr("Port:"), m_port);
    form->addRow(tr("Status:"), m_status);
    form->addRow(tr("Token:"), tokenRow);
    form->addRow(tr("Windows:"), m_windows);

    auto *config = new QWidget(this);
    auto *configLayout = new QVBoxLayout(config);
    configLayout->setContentsMargins(6, 6, 6, 6);
    configLayout->addWidget(intro);
    configLayout->addLayout(form);
    configLayout->addWidget(new QLabel(tr("Configuration for Claude:"), this));
    configLayout->addWidget(m_config);
    configLayout->addLayout(copyRow);

    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    auto *clear = new QPushButton(tr("Clear"), this);
    auto *logWidget = new QWidget(this);
    auto *logLayout = new QVBoxLayout(logWidget);
    logLayout->setContentsMargins(6, 0, 6, 6);
    auto *logHeader = new QHBoxLayout();
    logHeader->addWidget(new QLabel(tr("Log:"), this));
    logHeader->addStretch(1);
    logHeader->addWidget(clear);
    logLayout->addLayout(logHeader);
    logLayout->addWidget(m_log, 1);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(config);
    splitter->addWidget(logWidget);
    splitter->setStretchFactor(1, 1);
    setWidget(splitter);

    connect(m_enabled, &QCheckBox::toggled, this, &McpPanel::enabledToggled);
    connect(m_port, &QSpinBox::editingFinished, this, &McpPanel::applyPort);
    connect(copyToken, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(m_mcp->token()); });
    connect(newToken, &QPushButton::clicked, this, [this]() { m_mcp->regenerateToken(); });
    connect(copyConfig, &QPushButton::clicked, this,
            [this]() { QApplication::clipboard()->setText(m_mcp->mcpJsonSnippet()); });
    connect(copyCommand, &QPushButton::clicked, this,
            [this]() { QApplication::clipboard()->setText(m_mcp->claudeCodeCommand()); });
    connect(clear, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
    connect(m_mcp, &McpIntegration::stateChanged, this, &McpPanel::refresh);
    connect(m_mcp, &McpIntegration::logMessage, this, &McpPanel::appendLog);

    refresh();
}

//---------------------------------------------------------------------------------------------------------------------
void McpPanel::appendLog(const QString &message)
{
    m_log->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + message);
}

//---------------------------------------------------------------------------------------------------------------------
void McpPanel::enabledToggled(bool on)
{
    qApp->Seamly2DSettings()->SetMcpEnabled(on);
    if (on)
    {
        if (!m_mcp->isRunning())
        {
            m_mcp->start(static_cast<quint16>(m_port->value()));
        }
    }
    else
    {
        m_mcp->stop();
    }
    refresh();
}

//---------------------------------------------------------------------------------------------------------------------
void McpPanel::applyPort()
{
    const quint16 port = static_cast<quint16>(m_port->value());
    if (port == qApp->Seamly2DSettings()->GetMcpPort())
    {
        return;
    }
    qApp->Seamly2DSettings()->SetMcpPort(port);
    if (m_mcp->isRunning())
    {
        m_mcp->stop();
        m_mcp->start(port);   // other windows follow the new port when they are restarted
        appendLog(tr("Port changed to %1. Other open windows keep the old port until they are restarted.").arg(port));
    }
}

//---------------------------------------------------------------------------------------------------------------------
void McpPanel::refresh()
{
    {
        const QSignalBlocker blocker(m_enabled);
        m_enabled->setChecked(m_mcp->isRunning() || qApp->Seamly2DSettings()->GetMcpEnabled());
    }

    if (m_mcp->isRunning())
    {
        if (m_mcp->isHub())
        {
            m_status->setText(m_mcp->clientName().isEmpty()
                                  ? tr("Listening on 127.0.0.1:%1. This window answers Claude.").arg(m_mcp->port())
                                  : tr("Listening on 127.0.0.1:%1, connected: %2").arg(m_mcp->port()).arg(m_mcp->clientName()));
        }
        else
        {
            m_status->setText(tr("Port %1 is served by another Seamly2D window; this window is reached through it.")
                                  .arg(m_mcp->port()));
        }
        m_config->setPlainText(m_mcp->mcpJsonSnippet());
    }
    else
    {
        m_status->setText(m_enabled->isChecked() && !m_mcp->lastError().isEmpty()
                              ? tr("Not running: %1").arg(m_mcp->lastError())
                              : tr("Off"));
        m_config->setPlainText(tr("Turn the connection on to get the configuration."));
    }
    m_token->setText(m_mcp->token());
    m_token->setCursorPosition(0);   // show the start of the token, not its end

    QStringList lines;
    const QList<McpInstance> windows = m_mcp->windows();
    for (const McpInstance &window : windows)
    {
        const QString name = window.patternFile.isEmpty() ? (window.title.isEmpty() ? tr("(no pattern)") : window.title)
                                                          : QFileInfo(window.patternFile).fileName();
        lines << tr("%1%2 (id %3)%4").arg(name, window.modified ? QStringLiteral("*") : QString())
                     .arg(window.pid).arg(window.pid == m_mcp->windowId() ? tr(" - this window") : QString());
    }
    m_windows->setText(lines.isEmpty() ? tr("none") : lines.join(QLatin1Char('\n')));
}
