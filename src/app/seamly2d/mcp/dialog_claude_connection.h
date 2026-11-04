/***************************************************************************
 **  @file   dialog_claude_connection.h
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

#ifndef DIALOG_CLAUDE_CONNECTION_H
#define DIALOG_CLAUDE_CONNECTION_H

#include <QDialog>

class McpIntegration;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

class DialogClaudeConnection : public QDialog
{
    Q_OBJECT
public:
    DialogClaudeConnection(McpIntegration *mcp, QWidget *parent = nullptr);

private slots:
    void apply();
    void refresh();

private:
    McpIntegration *m_mcp;
    QCheckBox      *m_enabled;
    QSpinBox       *m_port;
    int             m_shownPort;   ///< port the field showed when the dialog opened
    QLabel         *m_status;
    QLineEdit      *m_token;
    QPlainTextEdit *m_config;
};

#endif // DIALOG_CLAUDE_CONNECTION_H
