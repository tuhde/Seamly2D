/***************************************************************************
 **  @file   claude_shell_panel.cpp
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

#include "claude_shell_panel.h"

#include <QEventLoop>
#include <QTimer>
#include <csignal>
#include <sys/types.h>

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QColor>
#include <QPalette>
#include <QTemporaryDir>
#include <QTextStream>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QVBoxLayout>
#include <QWidget>
#include <qtermwidget.h>

#include "mcp_integration.h"
#include "../core/application_2d.h"
#include "../../libs/vmisc/vsettings.h"

//---------------------------------------------------------------------------------------------------------------------
ClaudeShellPanel::ClaudeShellPanel(McpIntegration *mcp, QWidget *parent)
    : QDockWidget(tr("Claude shell"), parent)
    , m_mcp(mcp)
    , m_terminal(new QTermWidget(0, this))   // 0: do not start a shell yet
    , m_message(new QLabel(this))
    , m_restart(new QPushButton(tr("Restart session"), this))
    , m_config()
    , m_schemeDir()
    , m_schemeCounter(0)
    , m_started(false)
{
    setObjectName(QStringLiteral("claude_shell_DockWidget"));

    QWidget *content = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_message->setWordWrap(true);
    m_message->setVisible(false);
    m_restart->setVisible(false);
    m_terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);
    m_terminal->setFlowControlEnabled(false);
    applyColorScheme();

    // Seamly2D has single-letter shortcuts (tools, ...). While the terminal has the focus every key must reach the
    // shell, so the terminal and its child widgets have to accept the "shortcut override" event (see eventFilter).
    m_terminal->installEventFilter(this);
    const QList<QWidget *> children = m_terminal->findChildren<QWidget *>();
    for (QWidget *child : children)
    {
        child->installEventFilter(this);
    }

    layout->addWidget(m_message);
    layout->addWidget(m_restart);
    layout->addWidget(m_terminal, 1);
    setWidget(content);

    connect(m_restart, &QPushButton::clicked, this, &ClaudeShellPanel::startSession);
    connect(m_terminal, &QTermWidget::finished, this, [this]()
    {
        m_restart->setVisible(true);
    });
    // Start lazily: only when the panel is shown for the first time.
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible)
    {
        if (visible && !m_started)
        {
            startSession();
        }
    });
}

//---------------------------------------------------------------------------------------------------------------------
ClaudeShellPanel::~ClaudeShellPanel()
{
    // Stop the Claude CLI before the terminal is destroyed. Otherwise QTermWidget's process class finds it still
    // running, prints "the terminal process is still running, trying to stop it by SIGHUP" and kills it itself.
    if (!m_started)
    {
        return;
    }
    const int pid = m_terminal->getShellPID();
    if (pid <= 0 || ::kill(pid, 0) != 0)
    {
        return;   // never started or already finished
    }
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(m_terminal, &QTermWidget::finished, &loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    ::kill(pid, SIGHUP);
    timeout.start(2000);
    loop.exec();
}

//---------------------------------------------------------------------------------------------------------------------
bool ClaudeShellPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride)
    {
        // Accepting the override makes Qt deliver the key to the terminal instead of triggering a window shortcut.
        event->accept();
        return true;
    }
    return QDockWidget::eventFilter(watched, event);
}

//---------------------------------------------------------------------------------------------------------------------
void ClaudeShellPanel::changeEvent(QEvent *event)
{
    QDockWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
    {
        applyColorScheme();
    }
}

//---------------------------------------------------------------------------------------------------------------------
void ClaudeShellPanel::applyColorScheme()
{
    // Background and text come from the program's palette (the canvas look), so the shell matches the window in a
    // light or dark theme. The 16 ANSI colors are fixed sets that stay readable on a light or a dark background.
    const QPalette palette = qApp->palette();
    const QColor background = palette.color(QPalette::Base);
    const QColor foreground = palette.color(QPalette::Text);
    const bool dark = background.lightness() < 128;

    static const char *const lightNormal[8] = {"0,0,0", "170,0,0", "0,128,0", "150,100,0",
                                               "0,0,170", "150,0,150", "0,130,130", "90,90,90"};
    static const char *const lightIntense[8] = {"70,70,70", "200,30,30", "20,150,20", "170,120,0",
                                                "30,30,210", "180,30,180", "0,150,150", "40,40,40"};
    static const char *const darkNormal[8] = {"0,0,0", "230,90,90", "90,200,90", "220,170,60",
                                              "110,150,255", "210,110,210", "80,200,200", "210,210,210"};
    static const char *const darkIntense[8] = {"130,130,130", "255,120,120", "120,230,120", "245,200,90",
                                               "140,175,255", "235,140,235", "110,225,225", "255,255,255"};
    const char *const *normal = dark ? darkNormal : lightNormal;
    const char *const *intense = dark ? darkIntense : lightIntense;

    auto rgb = [](const QColor &c) { return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue()); };

    QString scheme;
    QTextStream out(&scheme);
    out << "[General]\nDescription=Seamly2D\nOpacity=1\n\n";
    out << "[Background]\nColor=" << rgb(background) << "\n\n[BackgroundIntense]\nColor=" << rgb(background) << "\n\n";
    out << "[Foreground]\nColor=" << rgb(foreground) << "\n\n[ForegroundIntense]\nColor=" << rgb(foreground) << "\n\n";
    for (int i = 0; i < 8; ++i)
    {
        out << "[Color" << i << "]\nColor=" << normal[i] << "\n\n";
        out << "[Color" << i << "Intense]\nColor=" << intense[i] << "\n\n";
    }
    out.flush();

    if (m_schemeDir.isNull())
    {
        m_schemeDir.reset(new QTemporaryDir());
    }
    if (!m_schemeDir->isValid())
    {
        return;
    }
    // A new file name per change: QTermWidget caches schemes by name.
    const QString path = m_schemeDir->filePath(QStringLiteral("seamly2d-%1.colorscheme").arg(++m_schemeCounter));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        file.write(scheme.toUtf8());
        file.close();
        m_terminal->setColorScheme(path);
    }
}

//---------------------------------------------------------------------------------------------------------------------
QString ClaudeShellPanel::findClaude()
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (path.isEmpty())
    {
        // GUI programs often do not have ~/.local/bin in PATH, where the installer puts the CLI.
        path = QStandardPaths::findExecutable(QStringLiteral("claude"),
                                              {QDir::homePath() + QStringLiteral("/.local/bin")});
    }
    return path;
}

//---------------------------------------------------------------------------------------------------------------------
void ClaudeShellPanel::showMessage(const QString &text)
{
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
}

//---------------------------------------------------------------------------------------------------------------------
void ClaudeShellPanel::startSession()
{
    const QString claude = findClaude();
    if (claude.isEmpty())
    {
        showMessage(tr("The Claude Code CLI ('claude') was not found. Install it, then press \"Restart session\"."));
        m_restart->setVisible(true);
        return;
    }

    // The shell talks to this window through the MCP server; make sure it is running.
    if (!m_mcp->isRunning() && !m_mcp->start(qApp->Seamly2DSettings()->GetMcpPort()))
    {
        showMessage(tr("The Claude connection could not be started: %1").arg(m_mcp->lastError()));
        m_restart->setVisible(true);
        return;
    }

    // The configuration contains the token, so keep it out of the process arguments: owner-only temporary file.
    m_config.reset(new QTemporaryFile(QDir::tempPath() + QStringLiteral("/seamly2d-mcp-XXXXXX.json")));
    if (!m_config->open())
    {
        showMessage(tr("Could not create the temporary MCP configuration: %1").arg(m_config->errorString()));
        m_restart->setVisible(true);
        return;
    }
    m_config->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_config->write(m_mcp->mcpJsonSnippet().toUtf8());
    m_config->flush();

    const QString hint = tr("You are running inside the Claude shell panel of Seamly2D. The 'seamly2d' MCP server "
                            "controls the pattern that is open in this window; use its tools to read and change "
                            "the pattern instead of editing files.");

    showMessage(QString());
    m_restart->setVisible(false);
    m_started = true;

    // Run in a dedicated empty folder, not in the user's home or pattern folder: Claude Code asks once whether to
    // trust the working folder, and that should never be a folder with unrelated files.
    const QString workDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/claude-shell");
    QDir().mkpath(workDir);
    m_terminal->setWorkingDirectory(workDir);
    m_terminal->setShellProgram(claude);
    m_terminal->setArgs({QStringLiteral("--mcp-config"), m_config->fileName(),
                         QStringLiteral("--append-system-prompt"), hint});
    m_terminal->startShellProgram();
    m_terminal->setFocus();
}
