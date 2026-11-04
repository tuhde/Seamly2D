/***************************************************************************
 **  @file   dialog_claude_connection.cpp
 **
 **  @brief  Dialog to switch the Claude (MCP) connection on and off and to copy its configuration.
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

#include "dialog_claude_connection.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "mcp_integration.h"
#include "../core/application_2d.h"
#include "../../libs/vmisc/vsettings.h"

//---------------------------------------------------------------------------------------------------------------------
DialogClaudeConnection::DialogClaudeConnection(McpIntegration *mcp, QWidget *parent)
    : QDialog(parent)
    , m_mcp(mcp)
    , m_enabled(new QCheckBox(tr("Allow Claude to connect to this window"), this))
    , m_port(new QSpinBox(this))
    , m_shownPort(0)
    , m_status(new QLabel(this))
    , m_token(new QLineEdit(this))
    , m_config(new QPlainTextEdit(this))
{
    setWindowTitle(tr("Claude connection"));
    setMinimumWidth(560);

    auto *intro = new QLabel(tr("Claude can create and change the pattern in this window. Changes appear at once "
                                "and can be undone. The connection is local to this computer and protected by a "
                                "token that changes every time Seamly2D starts."), this);
    intro->setWordWrap(true);

    m_port->setRange(0, 65535);
    // Show the port actually in use (it differs from the saved one after a fallback or a test override).
    m_shownPort = m_mcp->isRunning() ? m_mcp->port() : qApp->Seamly2DSettings()->GetMcpPort();
    m_port->setValue(m_shownPort);
    m_port->setToolTip(tr("0 picks a free port. If the port is taken, a free one is used instead."));
    m_enabled->setChecked(m_mcp->isRunning() || qApp->Seamly2DSettings()->GetMcpEnabled());

    m_token->setReadOnly(true);
    auto *copyToken = new QPushButton(tr("Copy"), this);
    auto *newToken = new QPushButton(tr("New token"), this);
    auto *tokenRow = new QHBoxLayout();
    tokenRow->addWidget(m_token, 1);
    tokenRow->addWidget(copyToken);
    tokenRow->addWidget(newToken);

    m_config->setReadOnly(true);
    m_config->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_config->setMinimumHeight(150);
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

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Configuration for Claude:"), this));
    layout->addWidget(m_config);
    layout->addLayout(copyRow);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &DialogClaudeConnection::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(copyToken, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(m_mcp->token()); });
    connect(copyConfig, &QPushButton::clicked, this,
            [this]() { QApplication::clipboard()->setText(m_mcp->mcpJsonSnippet()); });
    connect(copyCommand, &QPushButton::clicked, this,
            [this]() { QApplication::clipboard()->setText(m_mcp->claudeCodeCommand()); });
    connect(newToken, &QPushButton::clicked, this, [this]()
    {
        m_mcp->regenerateToken();
        refresh();
    });
    connect(m_mcp, &McpIntegration::stateChanged, this, &DialogClaudeConnection::refresh);

    refresh();
}

//---------------------------------------------------------------------------------------------------------------------
void DialogClaudeConnection::apply()
{
    VSettings *settings = qApp->Seamly2DSettings();
    const bool portChanged = m_port->value() != m_shownPort;
    settings->SetMcpEnabled(m_enabled->isChecked());
    if (portChanged)
    {
        settings->SetMcpPort(static_cast<quint16>(m_port->value()));   // do not overwrite it with a fallback port
    }

    if (m_enabled->isChecked())
    {
        if (!m_mcp->isRunning() || portChanged)
        {
            m_mcp->stop();
            m_mcp->start(static_cast<quint16>(m_port->value()));
        }
    }
    else
    {
        m_mcp->stop();
    }
    accept();
}

//---------------------------------------------------------------------------------------------------------------------
void DialogClaudeConnection::refresh()
{
    if (m_mcp->isRunning())
    {
        m_status->setText(m_mcp->clientName().isEmpty()
                              ? tr("Listening on 127.0.0.1:%1").arg(m_mcp->port())
                              : tr("Listening on 127.0.0.1:%1, connected: %2").arg(m_mcp->port()).arg(m_mcp->clientName()));
        m_token->setText(m_mcp->token());
        m_config->setPlainText(m_mcp->mcpJsonSnippet());
    }
    else
    {
        m_status->setText(m_enabled->isChecked() && !m_mcp->lastError().isEmpty()
                              ? tr("Not running: %1").arg(m_mcp->lastError())
                              : tr("Off"));
        m_token->setText(m_mcp->token());
        m_config->setPlainText(tr("Turn the connection on and press OK to get the configuration."));
    }
}
