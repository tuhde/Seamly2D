/***************************************************************************
 **  @file   mcpinstances.h
 **
 **  @brief  Directory of the running Seamly2D windows (processes) that offer MCP.
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

#ifndef MCPINSTANCES_H
#define MCPINSTANCES_H

#include <QList>
#include <QString>
#include <QtGlobal>

/// One running Seamly2D window (every window is its own process).
struct McpInstance
{
    qint64  pid = 0;
    quint16 port = 0;            ///< private loopback port of that window's MCP server
    QString patternFile;         ///< empty for a pattern that was never saved
    QString title;
    bool    modified = false;
    qint64  lastActivated = 0;   ///< ms since epoch of the last window activation

    bool operator==(const McpInstance &other) const;
};

/**
 * @brief The McpInstanceRegistry class publishes this window in a per-user directory, so that the window that owns
 * the public MCP port (the hub) can find all other windows and route tool calls to them.
 *
 * One small JSON file per process: `<dir>/<pid>.json`. Files of processes that no longer exist are ignored and
 * removed when the directory is read.
 */
class McpInstanceRegistry
{
public:
    /// Per-user directory (runtime dir, else temp dir). The SEAMLY_MCP_INSTANCE_DIR variable overrides it (tests).
    static QString defaultDirectory();

    explicit McpInstanceRegistry(const QString &directory = defaultDirectory(), qint64 pid = 0);

    qint64  pid() const { return m_pid; }
    /// Writes (or updates) this process' record. Does nothing when it is unchanged.
    bool    publish(const McpInstance &self);
    /// Removes this process' record.
    void    remove();
    /// All live instances, this one included, ordered by pid.
    QList<McpInstance> instances() const;

    static bool processExists(qint64 pid);

private:
    QString fileFor(qint64 pid) const;

    QString     m_directory;
    qint64      m_pid;
    McpInstance m_published;
    bool        m_hasPublished = false;
};

#endif // MCPINSTANCES_H
