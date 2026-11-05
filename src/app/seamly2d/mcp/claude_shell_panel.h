/***************************************************************************
 **  @file   claude_shell_panel.h
 **
 **  @brief  Dockable terminal that runs the Claude Code CLI against this window's MCP server.
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

#ifndef CLAUDE_SHELL_PANEL_H
#define CLAUDE_SHELL_PANEL_H

#include <QDockWidget>
#include <QScopedPointer>

class McpIntegration;
class QEvent;
class QLabel;
class QPushButton;
class QTemporaryDir;
class QTemporaryFile;
class QTermWidget;
class QVBoxLayout;

/**
 * @brief The ClaudeShellPanel class is a dock panel with an embedded terminal running the Claude Code CLI.
 *
 * The CLI is started with a temporary MCP configuration that points at this window's MCP server, so everything the
 * user asks in the shell is done through the in-process pattern tools (live on the canvas, one undo step per change).
 * The panel only exists when the program was built with QTermWidget (SEAMLY_HAVE_QTERMWIDGET).
 */
class ClaudeShellPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit ClaudeShellPanel(McpIntegration *mcp, QWidget *parent = nullptr);
    ~ClaudeShellPanel() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void startSession();
    void sessionFinished();

private:
    /// Path of the `claude` executable, or an empty string if it cannot be found.
    static QString findClaude();
    void showMessage(const QString &text);
    /// Creates a fresh terminal widget (colors, key handling) and puts it into the layout. Every session gets its
    /// own: a finished QTermWidget cannot be reliably started again.
    void createTerminal();
    /// Pins the terminal colors to the program's palette (canvas background/text color, light or dark).
    void applyColorScheme();

    McpIntegration *m_mcp;
    QVBoxLayout    *m_layout;
    QTermWidget    *m_terminal;
    QLabel         *m_message;
    QPushButton    *m_restart;
    QScopedPointer<QTemporaryFile> m_config; ///< MCP configuration with the token; owner-only, removed on exit.
    QScopedPointer<QTemporaryDir> m_schemeDir;  ///< generated *.colorscheme files
    int             m_schemeCounter;
    bool            m_started;
};

#endif // CLAUDE_SHELL_PANEL_H
