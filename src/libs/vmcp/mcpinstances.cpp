/***************************************************************************
 **  @file   mcpinstances.cpp
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

#include "mcpinstances.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

#if defined(Q_OS_WIN)
#   include <windows.h>
#else
#   include <cerrno>
#   include <csignal>
#   include <sys/types.h>
#endif

//---------------------------------------------------------------------------------------------------------------------
bool McpInstance::operator==(const McpInstance &other) const
{
    return pid == other.pid && port == other.port && patternFile == other.patternFile && title == other.title
        && modified == other.modified && lastActivated == other.lastActivated;
}

//---------------------------------------------------------------------------------------------------------------------
QString McpInstanceRegistry::defaultDirectory()
{
    if (qEnvironmentVariableIsSet("SEAMLY_MCP_INSTANCE_DIR"))
    {
        return qEnvironmentVariable("SEAMLY_MCP_INSTANCE_DIR");
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty())
    {
        base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    return base + QStringLiteral("/seamly2d-mcp");
}

//---------------------------------------------------------------------------------------------------------------------
McpInstanceRegistry::McpInstanceRegistry(const QString &directory, qint64 pid)
    : m_directory(directory)
    , m_pid(pid != 0 ? pid : QCoreApplication::applicationPid())
{
}

//---------------------------------------------------------------------------------------------------------------------
QString McpInstanceRegistry::fileFor(qint64 pid) const
{
    return m_directory + QLatin1Char('/') + QString::number(pid) + QStringLiteral(".json");
}

//---------------------------------------------------------------------------------------------------------------------
bool McpInstanceRegistry::publish(const McpInstance &self)
{
    McpInstance record = self;
    record.pid = m_pid;
    if (m_hasPublished && m_published == record)
    {
        return true;
    }

    QDir dir;
    if (!dir.mkpath(m_directory))
    {
        return false;
    }
    QFile::setPermissions(m_directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const QJsonObject json{{QStringLiteral("pid"), static_cast<double>(record.pid)},
                           {QStringLiteral("port"), record.port},
                           {QStringLiteral("patternFile"), record.patternFile},
                           {QStringLiteral("title"), record.title},
                           {QStringLiteral("modified"), record.modified},
                           {QStringLiteral("lastActivated"), static_cast<double>(record.lastActivated)}};
    QSaveFile file(fileFor(m_pid));   // atomic: a reader never sees a half-written file
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }
    file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!file.commit())
    {
        return false;
    }
    m_published = record;
    m_hasPublished = true;
    return true;
}

//---------------------------------------------------------------------------------------------------------------------
void McpInstanceRegistry::remove()
{
    QFile::remove(fileFor(m_pid));
    m_hasPublished = false;
}

//---------------------------------------------------------------------------------------------------------------------
QList<McpInstance> McpInstanceRegistry::instances() const
{
    QList<McpInstance> result;
    const QDir dir(m_directory);
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (const QString &name : files)
    {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
        {
            continue;
        }
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        file.close();

        McpInstance instance;
        instance.pid = static_cast<qint64>(json.value(QStringLiteral("pid")).toDouble());
        instance.port = static_cast<quint16>(json.value(QStringLiteral("port")).toInt());
        instance.patternFile = json.value(QStringLiteral("patternFile")).toString();
        instance.title = json.value(QStringLiteral("title")).toString();
        instance.modified = json.value(QStringLiteral("modified")).toBool();
        instance.lastActivated = static_cast<qint64>(json.value(QStringLiteral("lastActivated")).toDouble());
        if (instance.pid <= 0 || instance.port == 0)
        {
            continue;
        }
        if (!processExists(instance.pid))
        {
            QFile::remove(dir.filePath(name));   // left behind by a crashed window
            continue;
        }
        result.append(instance);
    }
    std::sort(result.begin(), result.end(), [](const McpInstance &a, const McpInstance &b) { return a.pid < b.pid; });
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
bool McpInstanceRegistry::processExists(qint64 pid)
{
#if defined(Q_OS_WIN)
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr)
    {
        return false;
    }
    DWORD exitCode = 0;
    const bool alive = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}
