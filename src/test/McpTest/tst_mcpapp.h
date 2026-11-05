/***************************************************************************
 **  @brief  Tests for the MCP server (Claude connection).
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
#ifndef TST_MCPAPP_H
#define TST_MCPAPP_H

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <memory>

/// Starts the real program (needs a display, e.g. xvfb-run) and drives the pattern tools through JSON-RPC over HTTP,
/// like Claude does. Skipped when the program binary has not been built or there is no display.
class TST_McpApp : public QObject
{
    Q_OBJECT
public:
    TST_McpApp() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void toolsAreListed();
    void drawingWithoutADraftBlockExplainsWhatToDo();
    void aDraftBlockSetsTheUnitAndBasePoint();
    void pointsAreComputedLikeTheDialogsDoIt();
    void invalidInputLeavesThePatternUnchanged();
    void variablesDriveTheDrawing();
    void objectsCanBeChangedAndDeleted();
    void curvesAndArcsReportTheirLength();
    void moreConstructionPointsAreComputed();
    void pathCurvesRunSmoothlyThroughPoints();
    void circleArcAndCurveIntersectionsAreComputed();
    void piecesReportAreaAndPerimeter();
    void pieceDetailsAreCuttingInstructions();
    void undoAndRedoStepBackThroughChanges();
    void savingAndReopeningGivesTheSamePattern();
    void exportWritesFiles();
    void screenshotReturnsAPng();
    void measurementValuesCanBeChanged();
    void measurementFileCanBeCreatedAndAttached();
    void multisizeTableFollowsSizeAndHeight();
    void severalWindowsShareOnePort();   ///< last: it stops the first window

private:
    bool        startApp();
    /// Starts a second window (own settings) that shares the port, token and instance directory of the first.
    bool        startPeer();
    void        stopPeer();
    void        stopApp();
    QJsonObject tool(const QString &name, const QJsonObject &arguments = QJsonObject());
    /// Calls a tool that must succeed and returns its structured result.
    QJsonObject ok(const QString &name, const QJsonObject &arguments = QJsonObject());
    /// Calls a tool that must fail and returns its message.
    QString     fails(const QString &name, const QJsonObject &arguments = QJsonObject());
    QJsonArray  objects(const QJsonObject &filter = QJsonObject());
    QJsonObject point(const QString &name);     ///< the entry with that name from list_objects
    QString     appBinary() const;

    QTemporaryDir               m_dataDir;      ///< SEAMLY_DATA_DIR: settings and logs of the test instance
    QTemporaryDir               m_workDir;      ///< files written by the tests
    QTemporaryDir               m_peerDataDir;  ///< settings and logs of the second window
    std::unique_ptr<QProcess>   m_peer;
    std::unique_ptr<QProcess>   m_app;
    quint16                     m_port = 0;
    QString                     m_token = QStringLiteral("integration-test-token");
    bool                        m_available = false;
    double                      m_baseX = 0;    ///< base point of the draft block (the drawing is relative to it)
    double                      m_baseY = 0;
    int                         m_straightCurve = 0;   ///< spline C-D with handles along the chord
    int                         m_saggingCurve = 0;    ///< spline A-B that sags into the rectangle
    int                         m_arc = 0;             ///< quarter circle around A, radius 15
};

#endif // TST_MCPAPP_H
