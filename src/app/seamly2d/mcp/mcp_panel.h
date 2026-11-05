/***************************************************************************
 **  @file   mcp_panel.h
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

#ifndef MCP_PANEL_H
#define MCP_PANEL_H

#include <QDockWidget>

class McpIntegration;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

/**
 * @brief The McpPanel class is the "MCP Config and Log" dock: switch the connection on and off, copy the token and
 * the client configuration, see the open windows and a log of the tool calls.
 */
class McpPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit McpPanel(McpIntegration *mcp, QWidget *parent = nullptr);

private slots:
    void refresh();
    void appendLog(const QString &message);
    void enabledToggled(bool on);
    void applyPort();
    void setConfigText(const QString &text);

private:
    McpIntegration *m_mcp;
    QCheckBox      *m_enabled;
    QSpinBox       *m_port;
    QLabel         *m_status;
    QLabel         *m_windows;
    QLineEdit      *m_token;
    QPlainTextEdit *m_config;
    QPlainTextEdit *m_log;
};

#endif // MCP_PANEL_H
