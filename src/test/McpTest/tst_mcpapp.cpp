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
#include "tst_mcpapp.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTcpServer>
#include <QtTest>

#include "mcptestclient.h"

namespace
{
QJsonObject obj(std::initializer_list<QPair<QString, QJsonValue>> items)
{
    QJsonObject result;
    for (const auto &item : items)
    {
        result.insert(item.first, item.second);
    }
    return result;
}

bool near(double a, double b, double tolerance = 0.01) { return qAbs(a - b) <= tolerance; }
} // namespace

//---------------------------------------------------------------------------------------------------------------------
QString TST_McpApp::appBinary() const
{
    // Same location the other tests use: <build>/src/test/McpTest/bin -> <build>/src/app/seamly2d/bin
    const QString path = QStringLiteral("/../../../app/seamly2d/bin/");
#if defined(Q_OS_WIN)
    return QCoreApplication::applicationDirPath() + path + QLatin1String("seamly2d.exe");
#elif defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath() + path + QLatin1String("Seamly2D.app/Contents/MacOS/seamly2d");
#else
    return QCoreApplication::applicationDirPath() + path + QLatin1String("seamly2d");
#endif
}

//---------------------------------------------------------------------------------------------------------------------
bool TST_McpApp::startApp()
{
    // A free port for the test instance.
    {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0))
        {
            return false;
        }
        m_port = probe.serverPort();
    }

    // Settings of the test instance: no first-run welcome dialog (it would block the start).
    const QString settingsDir = m_dataDir.path() + QLatin1String("/settings/Seamly2DTeam");
    QDir().mkpath(settingsDir);
    QDir().mkpath(m_dataDir.path() + QLatin1String("/logs/seamly2d"));
    {
        QSettings settings(settingsDir + QLatin1String("/qt6_seamly2d.ini"), QSettings::IniFormat);
        settings.setValue(QStringLiteral("configuration/showWelcome"), true);
        settings.sync();
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("SEAMLY_DATA_DIR"), m_dataDir.path());
    environment.insert(QStringLiteral("SEAMLY_MCP"), QStringLiteral("1"));
    environment.insert(QStringLiteral("SEAMLY_MCP_PORT"), QString::number(m_port));
    environment.insert(QStringLiteral("SEAMLY_MCP_TOKEN"), m_token);
    environment.insert(QStringLiteral("SEAMLY_MCP_ASSUME_YES"), QStringLiteral("1"));   // no dialogs to click
    environment.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));

    m_app.reset(new QProcess());
    m_app->setProcessEnvironment(environment);
    m_app->setWorkingDirectory(m_workDir.path());
    m_app->start(appBinary(), QStringList());
    if (!m_app->waitForStarted(20000))
    {
        return false;
    }

    // Wait until the server answers.
    const QJsonObject ping = obj({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 1},
                                  {QStringLiteral("method"), QStringLiteral("ping")}});
    for (int i = 0; i < 300; ++i)
    {
        if (m_app->state() != QProcess::Running)
        {
            return false;
        }
        if (rpc(m_port, m_token, ping, 2000).status == 200)
        {
            return true;
        }
        QTest::qWait(200);
    }
    return false;
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::stopApp()
{
    if (m_app && m_app->state() != QProcess::NotRunning)
    {
        m_app->terminate();
        if (!m_app->waitForFinished(5000))
        {
            m_app->kill();
            m_app->waitForFinished(5000);
        }
    }
    m_app.reset();
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::initTestCase()
{
    if (!QFileInfo::exists(appBinary()))
    {
        QSKIP(qPrintable(QStringLiteral("The program is not built (%1).").arg(appBinary())));
    }
#if defined(Q_OS_LINUX)
    // The program needs a display: CI runs "make check" under xvfb-run. (Qt's "offscreen" platform is not an option:
    // the main window does not finish starting there.)
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
    {
        QSKIP("No display. Run the tests under xvfb-run.");
    }
#endif
    QVERIFY(m_dataDir.isValid() && m_workDir.isValid());
    m_available = startApp();
    QVERIFY2(m_available, qPrintable(QStringLiteral("The program did not start or its MCP server did not answer.\n")
                                     + QString::fromUtf8(m_app ? m_app->readAllStandardError() : QByteArray())));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::cleanupTestCase()
{
    stopApp();
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject TST_McpApp::tool(const QString &name, const QJsonObject &arguments)
{
    const QJsonObject message = obj({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 1},
                                     {QStringLiteral("method"), QStringLiteral("tools/call")},
                                     {QStringLiteral("params"), obj({{QStringLiteral("name"), name},
                                                                     {QStringLiteral("arguments"), arguments}})}});
    const HttpReply reply = rpc(m_port, m_token, message, 120000);
    if (reply.status != 200)
    {
        qWarning() << "tool" << name << "HTTP status" << reply.status << reply.body;
    }
    return reply.json().value(QStringLiteral("result")).toObject();
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject TST_McpApp::ok(const QString &name, const QJsonObject &arguments)
{
    const QJsonObject result = tool(name, arguments);
    const QString text = result.value(QStringLiteral("content")).toArray().at(0).toObject()
                             .value(QStringLiteral("text")).toString();
    if (result.isEmpty() || result.value(QStringLiteral("isError")).toBool())
    {
        qWarning() << name << QJsonDocument(arguments).toJson(QJsonDocument::Compact) << "->" << text;
    }
    [&]() { QVERIFY2(!result.isEmpty() && !result.value(QStringLiteral("isError")).toBool(), qPrintable(text)); }();
    return result.value(QStringLiteral("structuredContent")).toObject();
}

//---------------------------------------------------------------------------------------------------------------------
QString TST_McpApp::fails(const QString &name, const QJsonObject &arguments)
{
    const QJsonObject result = tool(name, arguments);
    const QString text = result.value(QStringLiteral("content")).toArray().at(0).toObject()
                             .value(QStringLiteral("text")).toString();
    [&]() { QVERIFY2(result.value(QStringLiteral("isError")).toBool(),
                     qPrintable(QStringLiteral("%1 should have failed but said: %2").arg(name, text))); }();
    return text;
}

//---------------------------------------------------------------------------------------------------------------------
QJsonArray TST_McpApp::objects(const QJsonObject &filter)
{
    return ok(QStringLiteral("list_objects"), filter).value(QStringLiteral("objects")).toArray();
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject TST_McpApp::point(const QString &name)
{
    const QJsonArray all = objects(obj({{QStringLiteral("name_contains"), name}}));
    for (const QJsonValue &value : all)
    {
        if (value.toObject().value(QStringLiteral("name")).toString() == name)
        {
            return value.toObject();
        }
    }
    return QJsonObject();
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::toolsAreListed()
{
    const HttpReply reply = rpc(m_port, m_token,
                                obj({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 1},
                                     {QStringLiteral("method"), QStringLiteral("tools/list")}}));
    QStringList names;
    const QJsonArray tools = reply.json().value(QStringLiteral("result")).toObject().value(QStringLiteral("tools"))
                                 .toArray();
    for (const QJsonValue &tool : tools)
    {
        names << tool.toObject().value(QStringLiteral("name")).toString();
    }
    const QStringList expected{
        "get_pattern_info", "list_objects", "add_draft_block", "add_point", "add_line", "add_curve", "add_arc",
        "update_object", "delete_object", "list_variables", "set_variable", "delete_variable", "add_piece",
        "list_pieces", "delete_piece", "update_piece", "set_grainline", "set_notch", "show_mode", "fit_view", "screenshot", "save_pattern", "open_pattern",
        "export_layout"};
    for (const QString &name : expected)
    {
        QVERIFY2(names.contains(name), qPrintable(QStringLiteral("missing tool %1").arg(name)));
    }
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::drawingWithoutADraftBlockExplainsWhatToDo()
{
    QCOMPARE(ok(QStringLiteral("get_pattern_info")).value(QStringLiteral("draftBlocks")).toArray().size(), 0);
    const QString message = fails(QStringLiteral("add_point"),
                                  obj({{QStringLiteral("type"), QStringLiteral("endLine")},
                                       {QStringLiteral("base_point_id"), 1}, {QStringLiteral("length"), QStringLiteral("5")},
                                       {QStringLiteral("angle"), QStringLiteral("0")}}));
    QVERIFY2(message.contains(QStringLiteral("add_draft_block")), qPrintable(message));
    QVERIFY(fails(QStringLiteral("save_pattern")).contains(QStringLiteral("nothing to save")));
    QVERIFY(fails(QStringLiteral("add_piece"),
                  obj({{QStringLiteral("name"), QStringLiteral("X")},
                       {QStringLiteral("nodes"), QJsonArray{1, 2, 3}}})).contains(QStringLiteral("draft block")));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::aDraftBlockSetsTheUnitAndBasePoint()
{
    fails(QStringLiteral("add_draft_block"), obj({{QStringLiteral("unit"), QStringLiteral("furlong")}}));
    const QJsonObject created = ok(QStringLiteral("add_draft_block"),
                                   obj({{QStringLiteral("name"), QStringLiteral("Test")},
                                        {QStringLiteral("unit"), QStringLiteral("cm")}}));
    QCOMPARE(created.value(QStringLiteral("unit")).toString(), QStringLiteral("cm"));
    const QJsonObject base = created.value(QStringLiteral("basePoint")).toObject();
    QCOMPARE(base.value(QStringLiteral("id")).toInt(), 1);
    m_baseX = base.value(QStringLiteral("x")).toDouble();
    m_baseY = base.value(QStringLiteral("y")).toDouble();

    const QJsonObject info = ok(QStringLiteral("get_pattern_info"));
    QCOMPARE(info.value(QStringLiteral("unit")).toString(), QStringLiteral("cm"));
    QCOMPARE(info.value(QStringLiteral("activeDraftBlock")).toString(), QStringLiteral("Test"));

    QVERIFY(fails(QStringLiteral("add_draft_block"), obj({{QStringLiteral("name"), QStringLiteral("Test")}}))
                .contains(QStringLiteral("already exists")));
    QVERIFY(fails(QStringLiteral("add_draft_block"), obj({{QStringLiteral("name"), QStringLiteral("Other")},
                                                          {QStringLiteral("unit"), QStringLiteral("mm")}}))
                .contains(QStringLiteral("cannot be changed")));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::pointsAreComputedLikeTheDialogsDoIt()
{
    // A(1) is the base point. y grows downwards; angle 0 = right, 270 = down.
    QJsonObject b = ok(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("endLine")},
                                                         {QStringLiteral("base_point_id"), 1},
                                                         {QStringLiteral("length"), QStringLiteral("40")},
                                                         {QStringLiteral("angle"), QStringLiteral("0")},
                                                         {QStringLiteral("name"), QStringLiteral("B")}}));
    QVERIFY(near(b.value(QStringLiteral("x")).toDouble(), m_baseX + 40));
    QVERIFY(near(b.value(QStringLiteral("y")).toDouble(), m_baseY));

    const QJsonObject c = ok(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("endLine")},
                                                               {QStringLiteral("base_point_id"), 1},
                                                               {QStringLiteral("length"), QStringLiteral("20")},
                                                               {QStringLiteral("angle"), QStringLiteral("270")},
                                                               {QStringLiteral("name"), QStringLiteral("C")}}));
    QVERIFY(near(c.value(QStringLiteral("x")).toDouble(), m_baseX));
    QVERIFY(near(c.value(QStringLiteral("y")).toDouble(), m_baseY + 20));

    const QJsonObject d = ok(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("endLine")},
                                                               {QStringLiteral("base_point_id"), b.value(QStringLiteral("id"))},
                                                               {QStringLiteral("length"), QStringLiteral("20")},
                                                               {QStringLiteral("angle"), QStringLiteral("270")},
                                                               {QStringLiteral("name"), QStringLiteral("D")}}));
    QVERIFY(near(d.value(QStringLiteral("x")).toDouble(), m_baseX + 40));
    QVERIFY(near(d.value(QStringLiteral("y")).toDouble(), m_baseY + 20));

    // alongLine: "CurrentLength" is the length of the line between the two points.
    const QJsonObject mid = ok(QStringLiteral("add_point"),
                               obj({{QStringLiteral("type"), QStringLiteral("alongLine")},
                                    {QStringLiteral("base_point_id"), 1},
                                    {QStringLiteral("second_point_id"), b.value(QStringLiteral("id"))},
                                    {QStringLiteral("length"), QStringLiteral("CurrentLength/2")},
                                    {QStringLiteral("name"), QStringLiteral("M")}}));
    QVERIFY(near(mid.value(QStringLiteral("x")).toDouble(), m_baseX + 20));

    // normal: perpendicular to A->B; a positive length goes up on the screen for a line pointing right.
    const QJsonObject n = ok(QStringLiteral("add_point"),
                             obj({{QStringLiteral("type"), QStringLiteral("normal")},
                                  {QStringLiteral("base_point_id"), 1},
                                  {QStringLiteral("second_point_id"), b.value(QStringLiteral("id"))},
                                  {QStringLiteral("length"), QStringLiteral("5")}, {QStringLiteral("name"), QStringLiteral("N")}}));
    QVERIFY(near(n.value(QStringLiteral("x")).toDouble(), m_baseX));
    QVERIFY(near(n.value(QStringLiteral("y")).toDouble(), m_baseY - 5));

    // bisector of the angle B-A-C (90 degrees): 45 degrees down-right.
    const QJsonObject s = ok(QStringLiteral("add_point"),
                             obj({{QStringLiteral("type"), QStringLiteral("bisector")},
                                  {QStringLiteral("first_point_id"), b.value(QStringLiteral("id"))},
                                  {QStringLiteral("vertex_point_id"), 1},
                                  {QStringLiteral("third_point_id"), c.value(QStringLiteral("id"))},
                                  {QStringLiteral("length"), QStringLiteral("10")}, {QStringLiteral("name"), QStringLiteral("S")}}));
    QVERIFY(near(s.value(QStringLiteral("x")).toDouble(), m_baseX + 7.071));
    QVERIFY(near(s.value(QStringLiteral("y")).toDouble(), m_baseY + 7.071));

    // lineIntersect: the diagonals A-D and B-C of the rectangle cross at its center.
    const QJsonObject x = ok(QStringLiteral("add_point"),
                             obj({{QStringLiteral("type"), QStringLiteral("lineIntersect")},
                                  {QStringLiteral("line1_point1_id"), 1}, {QStringLiteral("line1_point2_id"), d.value(QStringLiteral("id"))},
                                  {QStringLiteral("line2_point1_id"), b.value(QStringLiteral("id"))},
                                  {QStringLiteral("line2_point2_id"), c.value(QStringLiteral("id"))},
                                  {QStringLiteral("name"), QStringLiteral("X")}}));
    QVERIFY(near(x.value(QStringLiteral("x")).toDouble(), m_baseX + 20));
    QVERIFY(near(x.value(QStringLiteral("y")).toDouble(), m_baseY + 10));

    // Lines: A-B, A-C and B-D are already drawn by the points; C-D is not.
    fails(QStringLiteral("add_line"), obj({{QStringLiteral("first_point_id"), 1},
                                           {QStringLiteral("second_point_id"), b.value(QStringLiteral("id"))}}));
    ok(QStringLiteral("add_line"), obj({{QStringLiteral("first_point_id"), c.value(QStringLiteral("id"))},
                                        {QStringLiteral("second_point_id"), d.value(QStringLiteral("id"))}}));
    fails(QStringLiteral("add_line"), obj({{QStringLiteral("first_point_id"), d.value(QStringLiteral("id"))},
                                           {QStringLiteral("second_point_id"), c.value(QStringLiteral("id"))}}));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::invalidInputLeavesThePatternUnchanged()
{
    const int before = ok(QStringLiteral("list_objects")).value(QStringLiteral("total")).toInt();
    const QString undoText = QStringLiteral("must stay");
    Q_UNUSED(undoText)
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("endLine")},
                                                    {QStringLiteral("base_point_id"), 1}, {QStringLiteral("length"), QStringLiteral("foo+")},
                                                    {QStringLiteral("angle"), QStringLiteral("0")}}))
                .contains(QStringLiteral("Invalid formula")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("endLine")},
                                                    {QStringLiteral("base_point_id"), 99}, {QStringLiteral("length"), QStringLiteral("1")},
                                                    {QStringLiteral("angle"), QStringLiteral("0")}}))
                .contains(QStringLiteral("does not exist")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("endLine")},
                                                    {QStringLiteral("base_point_id"), 1}, {QStringLiteral("length"), QStringLiteral("1")},
                                                    {QStringLiteral("angle"), QStringLiteral("0")}, {QStringLiteral("name"), QStringLiteral("B")}}))
                .contains(QStringLiteral("already used")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("spiral")}}))
                .contains(QStringLiteral("Unsupported")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("lineIntersect")},
                                                    {QStringLiteral("line1_point1_id"), 1}, {QStringLiteral("line1_point2_id"), 2},
                                                    {QStringLiteral("line2_point1_id"), 3}, {QStringLiteral("line2_point2_id"), 4}}))
                .isEmpty() == false);   // A-B and C-D are parallel
    QCOMPARE(ok(QStringLiteral("list_objects")).value(QStringLiteral("total")).toInt(), before);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::variablesDriveTheDrawing()
{
    const QJsonObject w = ok(QStringLiteral("set_variable"), obj({{QStringLiteral("name"), QStringLiteral("skirt_w")},
                                                                  {QStringLiteral("formula"), QStringLiteral("30")}}));
    QCOMPARE(w.value(QStringLiteral("reference")).toString(), QStringLiteral("#skirt_w"));
    ok(QStringLiteral("set_variable"), obj({{QStringLiteral("name"), QStringLiteral("half")},
                                            {QStringLiteral("formula"), QStringLiteral("#skirt_w/2")}}));
    const QJsonObject e = ok(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("endLine")},
                                                               {QStringLiteral("base_point_id"), 1},
                                                               {QStringLiteral("length"), QStringLiteral("#half")},
                                                               {QStringLiteral("angle"), QStringLiteral("180")},
                                                               {QStringLiteral("name"), QStringLiteral("E")}}));
    QVERIFY(near(e.value(QStringLiteral("x")).toDouble(), m_baseX - 15));

    // One change of the variable moves everything that depends on it.
    ok(QStringLiteral("set_variable"), obj({{QStringLiteral("name"), QStringLiteral("skirt_w")},
                                            {QStringLiteral("formula"), QStringLiteral("50")}}));
    QVERIFY(near(point(QStringLiteral("E")).value(QStringLiteral("x")).toDouble(), m_baseX - 25));

    // Bad input is refused and changes nothing.
    QVERIFY(fails(QStringLiteral("set_variable"), obj({{QStringLiteral("name"), QStringLiteral("#bad")},
                                                       {QStringLiteral("formula"), QStringLiteral("1")}}))
                .contains(QStringLiteral("not valid")));
    QVERIFY(fails(QStringLiteral("set_variable"), obj({{QStringLiteral("name"), QStringLiteral("skirt_w")},
                                                       {QStringLiteral("formula"), QStringLiteral("#half")}}))
                .contains(QStringLiteral("depend on itself")));
    QVERIFY(fails(QStringLiteral("set_variable"), obj({{QStringLiteral("name"), QStringLiteral("fresh")}}))
                .contains(QStringLiteral("'formula' is required")));
    QVERIFY(fails(QStringLiteral("delete_variable"), obj({{QStringLiteral("name"), QStringLiteral("half")}}))
                .contains(QStringLiteral("still used")));

    const QJsonArray variables = ok(QStringLiteral("list_variables")).value(QStringLiteral("variables")).toArray();
    QCOMPARE(variables.size(), 2);
    QCOMPARE(variables.at(0).toObject().value(QStringLiteral("value")).toDouble(), 50.0);
    QCOMPARE(variables.at(1).toObject().value(QStringLiteral("value")).toDouble(), 25.0);
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::objectsCanBeChangedAndDeleted()
{
    const int idE = point(QStringLiteral("E")).value(QStringLiteral("id")).toInt();
    QVERIFY(idE > 0);
    // Changing the length of B moves what is placed relative to B.
    const int idB = point(QStringLiteral("B")).value(QStringLiteral("id")).toInt();
    const double dBefore = point(QStringLiteral("D")).value(QStringLiteral("x")).toDouble();
    ok(QStringLiteral("update_object"), obj({{QStringLiteral("id"), idB}, {QStringLiteral("length"), QStringLiteral("60")}}));
    QVERIFY(near(point(QStringLiteral("B")).value(QStringLiteral("x")).toDouble(), m_baseX + 60));
    QVERIFY(near(point(QStringLiteral("D")).value(QStringLiteral("x")).toDouble(), dBefore + 20));
    ok(QStringLiteral("update_object"), obj({{QStringLiteral("id"), idB}, {QStringLiteral("length"), QStringLiteral("40")}}));
    QVERIFY(near(point(QStringLiteral("D")).value(QStringLiteral("x")).toDouble(), dBefore));

    // Refused: unknown argument, bad formula, a name that is used in formulas, a reference to a later object.
    QVERIFY(fails(QStringLiteral("update_object"), obj({{QStringLiteral("id"), idB}, {QStringLiteral("colour"), QStringLiteral("red")}}))
                .contains(QStringLiteral("cannot be changed")));
    QVERIFY(fails(QStringLiteral("update_object"), obj({{QStringLiteral("id"), idB}, {QStringLiteral("length"), QStringLiteral("x+")}}))
                .contains(QStringLiteral("Invalid formula")));
    QVERIFY(fails(QStringLiteral("update_object"), obj({{QStringLiteral("id"), idB}, {QStringLiteral("base_point_id"), idE}}))
                .contains(QStringLiteral("earlier")));

    // Deleting: the base point and objects that others use are refused; a free leaf is deleted.
    fails(QStringLiteral("delete_object"), obj({{QStringLiteral("id"), 1}}));
    QVERIFY(fails(QStringLiteral("delete_object"), obj({{QStringLiteral("id"), idB}})).contains(QStringLiteral("still used")));
    ok(QStringLiteral("delete_object"), obj({{QStringLiteral("id"), idE}}));
    QVERIFY(point(QStringLiteral("E")).isEmpty());
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::curvesAndArcsReportTheirLength()
{
    const int idC = point(QStringLiteral("C")).value(QStringLiteral("id")).toInt();
    const int idD = point(QStringLiteral("D")).value(QStringLiteral("id")).toInt();
    const int idA = 1;
    const int idB = point(QStringLiteral("B")).value(QStringLiteral("id")).toInt();

    // Handles along the chord make a straight line: the length is the distance C-D (40 cm).
    const QJsonObject straight = ok(QStringLiteral("add_curve"),
                                    obj({{QStringLiteral("type"), QStringLiteral("spline")}, {QStringLiteral("start_point_id"), idC},
                                         {QStringLiteral("end_point_id"), idD}, {QStringLiteral("angle1"), QStringLiteral("0")},
                                         {QStringLiteral("angle2"), QStringLiteral("180")}, {QStringLiteral("length1"), QStringLiteral("10")},
                                         {QStringLiteral("length2"), QStringLiteral("10")}}));
    QVERIFY(near(straight.value(QStringLiteral("length")).toDouble(), 40.0));
    m_straightCurve = straight.value(QStringLiteral("id")).toInt();

    // A sagging curve is longer than its chord.
    const QJsonObject sagging = ok(QStringLiteral("add_curve"),
                                   obj({{QStringLiteral("type"), QStringLiteral("spline")}, {QStringLiteral("start_point_id"), idA},
                                        {QStringLiteral("end_point_id"), idB}, {QStringLiteral("angle1"), QStringLiteral("350")},
                                        {QStringLiteral("angle2"), QStringLiteral("190")}, {QStringLiteral("length1"), QStringLiteral("12")},
                                        {QStringLiteral("length2"), QStringLiteral("12")}}));
    QVERIFY(sagging.value(QStringLiteral("length")).toDouble() > 40.0);
    m_saggingCurve = sagging.value(QStringLiteral("id")).toInt();

    const QJsonObject bezier = ok(QStringLiteral("add_curve"),
                                  obj({{QStringLiteral("type"), QStringLiteral("cubicBezier")}, {QStringLiteral("start_point_id"), idC},
                                       {QStringLiteral("control1_point_id"), idA}, {QStringLiteral("control2_point_id"), idB},
                                       {QStringLiteral("end_point_id"), idD}}));
    QVERIFY(bezier.value(QStringLiteral("length")).toDouble() > 40.0);

    // A quarter circle of radius 15: length = pi/2 * 15.
    const QJsonObject arc = ok(QStringLiteral("add_arc"), obj({{QStringLiteral("center_point_id"), idA}, {QStringLiteral("radius"), QStringLiteral("15")},
                                                              {QStringLiteral("start_angle"), QStringLiteral("270")},
                                                              {QStringLiteral("end_angle"), QStringLiteral("360")}}));
    QVERIFY(near(arc.value(QStringLiteral("length")).toDouble(), M_PI / 2 * 15, 0.01));

    QVERIFY(fails(QStringLiteral("add_arc"), obj({{QStringLiteral("center_point_id"), idA}, {QStringLiteral("radius"), QStringLiteral("0")},
                                                  {QStringLiteral("start_angle"), QStringLiteral("0")}, {QStringLiteral("end_angle"), QStringLiteral("90")}}))
                .contains(QStringLiteral("greater than 0")));
    fails(QStringLiteral("add_curve"), obj({{QStringLiteral("type"), QStringLiteral("spline")}, {QStringLiteral("start_point_id"), idC},
                                            {QStringLiteral("end_point_id"), idC}, {QStringLiteral("angle1"), QStringLiteral("0")},
                                            {QStringLiteral("angle2"), QStringLiteral("0")}, {QStringLiteral("length1"), QStringLiteral("1")},
                                            {QStringLiteral("length2"), QStringLiteral("1")}}));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::piecesReportAreaAndPerimeter()
{
    const int a = 1;
    const int b = point(QStringLiteral("B")).value(QStringLiteral("id")).toInt();
    const int c = point(QStringLiteral("C")).value(QStringLiteral("id")).toInt();
    const int d = point(QStringLiteral("D")).value(QStringLiteral("id")).toInt();

    // The rectangle A-B-D-C is 40 x 20 cm.
    const QJsonObject front = ok(QStringLiteral("add_piece"),
                                 obj({{QStringLiteral("name"), QStringLiteral("Front")}, {QStringLiteral("nodes"), QJsonArray{a, b, d, c}},
                                      {QStringLiteral("seam_allowance"), true}, {QStringLiteral("seam_allowance_width"), QStringLiteral("1")}}));
    QVERIFY(near(front.value(QStringLiteral("area")).toDouble(), 800.0));
    QVERIFY(near(front.value(QStringLiteral("perimeter")).toDouble(), 120.0));
    QVERIFY(front.value(QStringLiteral("seamAllowance")).toBool());
    QVERIFY(near(front.value(QStringLiteral("seamAllowanceWidth")).toDouble(), 1.0));

    // The direction of a curve is detected: the straight curve C-D is traversed D -> C in this outline, against its own
    // direction, and the piece must still enclose exactly the rectangle.
    QVERIFY(m_straightCurve > 0 && m_saggingCurve > 0);
    const QJsonObject hem = ok(QStringLiteral("add_piece"),
                               obj({{QStringLiteral("name"), QStringLiteral("Hem")},
                                    {QStringLiteral("nodes"), QJsonArray{a, b, d, m_straightCurve, c}}}));
    QVERIFY(near(hem.value(QStringLiteral("area")).toDouble(), 800.0, 0.5));
    // A curve that sags into the rectangle takes area away: the curve really is part of the outline.
    const QJsonObject bowed = ok(QStringLiteral("add_piece"),
                                 obj({{QStringLiteral("name"), QStringLiteral("Bowed")},
                                      {QStringLiteral("nodes"), QJsonArray{a, m_saggingCurve, b, d, c}}}));
    QVERIFY2(bowed.value(QStringLiteral("area")).toDouble() < 795.0, qPrintable(QJsonDocument(bowed).toJson()));
    ok(QStringLiteral("delete_piece"), obj({{QStringLiteral("id"), bowed.value(QStringLiteral("id"))}}));
    ok(QStringLiteral("delete_piece"), obj({{QStringLiteral("id"), hem.value(QStringLiteral("id"))}}));

    QVERIFY(fails(QStringLiteral("add_piece"), obj({{QStringLiteral("name"), QStringLiteral("Front")}, {QStringLiteral("nodes"), QJsonArray{a, b, d}}}))
                .contains(QStringLiteral("already exists")));
    QVERIFY(fails(QStringLiteral("add_piece"), obj({{QStringLiteral("name"), QStringLiteral("Line")}, {QStringLiteral("nodes"), QJsonArray{a, b, b}}}))
                .contains(QStringLiteral("twice")));
    QVERIFY(fails(QStringLiteral("add_piece"), obj({{QStringLiteral("name"), QStringLiteral("Flat")}, {QStringLiteral("nodes"), QJsonArray{a, point(QStringLiteral("M")).value(QStringLiteral("id")), b}}}))
                .contains(QStringLiteral("area")));
    QVERIFY(fails(QStringLiteral("add_piece"), obj({{QStringLiteral("name"), QStringLiteral("Wide")}, {QStringLiteral("nodes"), QJsonArray{a, b, d}},
                                                    {QStringLiteral("seam_allowance"), true}, {QStringLiteral("seam_allowance_width"), QStringLiteral("-1")}}))
                .contains(QStringLiteral("negative")));

    const QJsonArray pieces = ok(QStringLiteral("list_pieces")).value(QStringLiteral("pieces")).toArray();
    QCOMPARE(pieces.size(), 1);
    QCOMPARE(pieces.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Front"));

    ok(QStringLiteral("show_mode"), obj({{QStringLiteral("mode"), QStringLiteral("piece")}}));
    ok(QStringLiteral("fit_view"), obj({{QStringLiteral("target"), QStringLiteral("piece")},
                                        {QStringLiteral("piece_id"), pieces.at(0).toObject().value(QStringLiteral("id"))}}));
    ok(QStringLiteral("show_mode"), obj({{QStringLiteral("mode"), QStringLiteral("draft")}}));
    ok(QStringLiteral("fit_view"), obj({{QStringLiteral("target"), QStringLiteral("block")}}));
    fails(QStringLiteral("fit_view"), obj({{QStringLiteral("target"), QStringLiteral("piece")}, {QStringLiteral("piece_id"), 1}}));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::pieceDetailsAreCuttingInstructions()
{
    const QJsonObject front = ok(QStringLiteral("list_pieces")).value(QStringLiteral("pieces")).toArray().at(0).toObject();
    const int id = front.value(QStringLiteral("id")).toInt();
    const int pointB = point(QStringLiteral("B")).value(QStringLiteral("id")).toInt();
    const int pointC = point(QStringLiteral("C")).value(QStringLiteral("id")).toInt();
    QCOMPARE(front.value(QStringLiteral("quantity")).toInt(), 1);
    QVERIFY(!front.value(QStringLiteral("grainline")).toObject().value(QStringLiteral("visible")).toBool());
    QVERIFY(front.value(QStringLiteral("notches")).toArray().isEmpty());

    // Cutting instructions and label texts.
    QJsonObject changed = ok(QStringLiteral("update_piece"),
                             obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("quantity"), 2},
                                  {QStringLiteral("letter"), QStringLiteral("A")}, {QStringLiteral("annotation"), QStringLiteral("cut 2 mirrored")},
                                  {QStringLiteral("on_fold"), true}}));
    QCOMPARE(changed.value(QStringLiteral("quantity")).toInt(), 2);
    QCOMPARE(changed.value(QStringLiteral("letter")).toString(), QStringLiteral("A"));
    QCOMPARE(changed.value(QStringLiteral("annotation")).toString(), QStringLiteral("cut 2 mirrored"));
    QVERIFY(changed.value(QStringLiteral("onFold")).toBool());
    ok(QStringLiteral("update_piece"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("on_fold"), false}}));
    changed = ok(QStringLiteral("update_piece"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("seam_allowance_width"), QStringLiteral("1.5")}}));
    QVERIFY(near(changed.value(QStringLiteral("seamAllowanceWidth")).toDouble(), 1.5));
    ok(QStringLiteral("update_piece"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("seam_allowance_width"), QStringLiteral("1")}}));
    changed = ok(QStringLiteral("update_piece"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("name"), QStringLiteral("Bodice")}}));
    QCOMPARE(changed.value(QStringLiteral("name")).toString(), QStringLiteral("Bodice"));
    ok(QStringLiteral("update_piece"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("name"), QStringLiteral("Front")}}));

    QVERIFY(fails(QStringLiteral("update_piece"), obj({{QStringLiteral("piece_id"), id}})).contains(QStringLiteral("Nothing to change")));
    QVERIFY(fails(QStringLiteral("update_piece"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("quantity"), 0}})).contains(QStringLiteral("whole number")));
    QVERIFY(fails(QStringLiteral("update_piece"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("colour"), QStringLiteral("red")}})).contains(QStringLiteral("cannot be changed")));
    QVERIFY(fails(QStringLiteral("update_piece"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("seam_allowance_width"), QStringLiteral("-1")}})).contains(QStringLiteral("negative")));
    QVERIFY(fails(QStringLiteral("update_piece"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("name"), QStringLiteral("")}})).contains(QStringLiteral("empty")));
    QVERIFY(fails(QStringLiteral("update_piece"), obj({{QStringLiteral("piece_id"), 9999}, {QStringLiteral("quantity"), 1}})).contains(QStringLiteral("no piece")));

    // Grainline: the defaults are vertical, 60 % of the piece height (20 cm) = 12 cm, arrows at both ends.
    QJsonObject grainline = ok(QStringLiteral("set_grainline"), obj({{QStringLiteral("piece_id"), id}})).value(QStringLiteral("grainline")).toObject();
    QVERIFY(grainline.value(QStringLiteral("visible")).toBool());
    QCOMPARE(grainline.value(QStringLiteral("angle")).toString(), QStringLiteral("90"));
    QCOMPARE(grainline.value(QStringLiteral("length")).toString(), QStringLiteral("12"));
    grainline = ok(QStringLiteral("set_grainline"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("angle"), QStringLiteral("0")},
                                                         {QStringLiteral("length"), QStringLiteral("25")}, {QStringLiteral("arrows"), QStringLiteral("top")}}))
                       .value(QStringLiteral("grainline")).toObject();
    QCOMPARE(grainline.value(QStringLiteral("angle")).toString(), QStringLiteral("0"));
    QCOMPARE(grainline.value(QStringLiteral("length")).toString(), QStringLiteral("25"));
    QVERIFY(!ok(QStringLiteral("set_grainline"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("visible"), false}}))
                 .value(QStringLiteral("grainline")).toObject().value(QStringLiteral("visible")).toBool());
    QVERIFY(fails(QStringLiteral("set_grainline"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("arrows"), QStringLiteral("left")}})).contains(QStringLiteral("arrows")));
    QVERIFY(fails(QStringLiteral("set_grainline"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("length"), QStringLiteral("0")}})).contains(QStringLiteral("greater than 0")));
    QVERIFY(fails(QStringLiteral("set_grainline"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("angle"), QStringLiteral("x+")}})).contains(QStringLiteral("Invalid formula")));
    ok(QStringLiteral("set_grainline"), obj({{QStringLiteral("piece_id"), id}}));   // left on, with the defaults

    // Notches: visible by default (on the cut line too, the piece has a seam allowance); change; remove.
    QJsonArray notches = ok(QStringLiteral("set_notch"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), pointB}}))
                             .value(QStringLiteral("notches")).toArray();
    QCOMPARE(notches.size(), 1);
    QCOMPARE(notches.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("B"));
    QCOMPARE(notches.at(0).toObject().value(QStringLiteral("count")).toInt(), 1);
    QVERIFY(notches.at(0).toObject().value(QStringLiteral("onCutline")).toBool());
    QVERIFY(notches.at(0).toObject().value(QStringLiteral("onSeamline")).toBool());
    notches = ok(QStringLiteral("set_notch"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), pointB},
                                                   {QStringLiteral("type"), QStringLiteral("castle")}, {QStringLiteral("count"), 3}}))
                  .value(QStringLiteral("notches")).toArray();
    QCOMPARE(notches.size(), 1);
    QCOMPARE(notches.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("castle"));
    QCOMPARE(notches.at(0).toObject().value(QStringLiteral("count")).toInt(), 3);
    notches = ok(QStringLiteral("set_notch"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), pointB}, {QStringLiteral("notch"), false}}))
                  .value(QStringLiteral("notches")).toArray();
    QVERIFY(notches.isEmpty());

    const int pointM = point(QStringLiteral("M")).value(QStringLiteral("id")).toInt();   // not on the outline
    QVERIFY(fails(QStringLiteral("set_notch"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), pointM}})).contains(QStringLiteral("not a point of the outline")));
    QVERIFY(fails(QStringLiteral("set_notch"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), pointB}, {QStringLiteral("type"), QStringLiteral("zigzag")}})).contains(QStringLiteral("'type' must be one of")));
    QVERIFY(fails(QStringLiteral("set_notch"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), pointB}, {QStringLiteral("count"), 4}})).contains(QStringLiteral("1, 2 or 3")));
    QVERIFY(fails(QStringLiteral("set_notch"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), pointB}, {QStringLiteral("length"), -1}})).contains(QStringLiteral("greater than 0")));
    QVERIFY(fails(QStringLiteral("set_notch"), obj({{QStringLiteral("piece_id"), id}})).contains(QStringLiteral("point_id")));
    // A notch that stays, so that the save/open round trip has to keep it.
    notches = ok(QStringLiteral("set_notch"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), pointC},
                                                   {QStringLiteral("type"), QStringLiteral("t")}, {QStringLiteral("count"), 2}}))
                  .value(QStringLiteral("notches")).toArray();
    QCOMPARE(notches.size(), 1);

    // Everything above is one undo step per call and shows up in list_pieces.
    const QJsonObject listed = ok(QStringLiteral("list_pieces")).value(QStringLiteral("pieces")).toArray().at(0).toObject();
    QCOMPARE(listed.value(QStringLiteral("quantity")).toInt(), 2);
    QCOMPARE(listed.value(QStringLiteral("notches")).toArray().size(), 1);
    QVERIFY(listed.value(QStringLiteral("grainline")).toObject().value(QStringLiteral("visible")).toBool());
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::savingAndReopeningGivesTheSamePattern()
{
    const QString file = m_workDir.path() + QLatin1String("/roundtrip");
    const QJsonObject saved = ok(QStringLiteral("save_pattern"), obj({{QStringLiteral("path"), file}}));
    QCOMPARE(saved.value(QStringLiteral("path")).toString(), file + QLatin1String(".sm2d"));
    QVERIFY(QFileInfo::exists(file + QLatin1String(".sm2d")));

    // Path rules: only absolute .sm2d paths in existing folders.
    QVERIFY(fails(QStringLiteral("save_pattern"), obj({{QStringLiteral("path"), QStringLiteral("relative.sm2d")}}))
                .contains(QStringLiteral("absolute")));
    QVERIFY(fails(QStringLiteral("save_pattern"), obj({{QStringLiteral("path"), m_workDir.path() + QLatin1String("/x.txt")}}))
                .contains(QStringLiteral(".sm2d")));
    QVERIFY(fails(QStringLiteral("save_pattern"), obj({{QStringLiteral("path"), m_workDir.path() + QLatin1String("/nope/x.sm2d")}}))
                .contains(QStringLiteral("does not exist")));
    // A window that holds a pattern cannot open another one.
    QVERIFY(fails(QStringLiteral("open_pattern"), obj({{QStringLiteral("path"), file + QLatin1String(".sm2d")}}))
                .contains(QStringLiteral("already contains")));

    // What the file says.
    QFile f(file + QLatin1String(".sm2d"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString xml = QString::fromUtf8(f.readAll());
    QVERIFY(xml.contains(QStringLiteral("name=\"#skirt_w\"")));
    QVERIFY(xml.contains(QStringLiteral("name=\"Front\"")));

    // Remember the pattern, restart the program and open the file again.
    const QJsonArray objectsBefore = objects();
    const QJsonArray variablesBefore = ok(QStringLiteral("list_variables")).value(QStringLiteral("variables")).toArray();
    const QJsonArray piecesBefore = ok(QStringLiteral("list_pieces")).value(QStringLiteral("pieces")).toArray();
    stopApp();
    QVERIFY(startApp());
    QCOMPARE(ok(QStringLiteral("get_pattern_info")).value(QStringLiteral("draftBlocks")).toArray().size(), 0);

    fails(QStringLiteral("open_pattern"), obj({{QStringLiteral("path"), m_workDir.path() + QLatin1String("/missing.sm2d")}}));
    ok(QStringLiteral("open_pattern"), obj({{QStringLiteral("path"), file + QLatin1String(".sm2d")}}));

    const QJsonArray objectsAfter = objects();
    QCOMPARE(objectsAfter.size(), objectsBefore.size());
    for (int i = 0; i < objectsBefore.size(); ++i)
    {
        const QJsonObject before = objectsBefore.at(i).toObject();
        const QJsonObject after = objectsAfter.at(i).toObject();
        QCOMPARE(after.value(QStringLiteral("id")), before.value(QStringLiteral("id")));
        QCOMPARE(after.value(QStringLiteral("name")), before.value(QStringLiteral("name")));
        QVERIFY(near(after.value(QStringLiteral("x")).toDouble(), before.value(QStringLiteral("x")).toDouble(), 0.001));
        QVERIFY(near(after.value(QStringLiteral("y")).toDouble(), before.value(QStringLiteral("y")).toDouble(), 0.001));
    }
    const QJsonArray variablesAfter = ok(QStringLiteral("list_variables")).value(QStringLiteral("variables")).toArray();
    QCOMPARE(variablesAfter, variablesBefore);
    QCOMPARE(ok(QStringLiteral("list_pieces")).value(QStringLiteral("pieces")).toArray(), piecesBefore);
    QCOMPARE(ok(QStringLiteral("get_pattern_info")).value(QStringLiteral("unit")).toString(), QStringLiteral("cm"));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::exportWritesFiles()
{
    const QString dir = m_workDir.path() + QLatin1String("/export");

    // Arranged on pages (PDF): the 40 x 20 cm piece fits on A1.
    const QJsonObject layout = ok(QStringLiteral("export_layout"),
                                  obj({{QStringLiteral("directory"), dir}, {QStringLiteral("name"), QStringLiteral("skirt")},
                                       {QStringLiteral("format"), QStringLiteral("pdf")}, {QStringLiteral("paper"), QStringLiteral("A1")}}));
    QCOMPARE(layout.value(QStringLiteral("pages")).toInt(), 1);
    const QJsonArray files = layout.value(QStringLiteral("files")).toArray();
    QCOMPARE(files.size(), 1);
    QFile pdf(files.at(0).toObject().value(QStringLiteral("path")).toString());
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    QVERIFY(pdf.read(5) == "%PDF-");

    // One file per piece (SVG).
    const QJsonObject pieces = ok(QStringLiteral("export_layout"),
                                  obj({{QStringLiteral("directory"), dir}, {QStringLiteral("name"), QStringLiteral("single")},
                                       {QStringLiteral("format"), QStringLiteral("svg")}, {QStringLiteral("mode"), QStringLiteral("pieces")}}));
    QFile svg(pieces.value(QStringLiteral("files")).toArray().at(0).toObject().value(QStringLiteral("path")).toString());
    QVERIFY(svg.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(svg.readAll()).contains(QStringLiteral("<svg")));

    // Cutting software format.
    const QJsonObject dxf = ok(QStringLiteral("export_layout"),
                               obj({{QStringLiteral("directory"), dir}, {QStringLiteral("name"), QStringLiteral("cut")},
                                    {QStringLiteral("format"), QStringLiteral("dxf")}, {QStringLiteral("paper"), QStringLiteral("A0")}}));
    QVERIFY(dxf.value(QStringLiteral("files")).toArray().at(0).toObject().value(QStringLiteral("bytes")).toInt() > 100);

    // Refused before anything is written.
    QVERIFY(fails(QStringLiteral("export_layout"), obj({{QStringLiteral("directory"), dir}, {QStringLiteral("name"), QStringLiteral("a4")},
                                                        {QStringLiteral("paper"), QStringLiteral("A4")}}))
                .contains(QStringLiteral("does not fit")));
    QVERIFY(!QFileInfo::exists(dir + QLatin1String("/a4_layout_01.pdf")));
    QVERIFY(fails(QStringLiteral("export_layout"), obj({{QStringLiteral("directory"), QStringLiteral("relative")}})).contains(QStringLiteral("absolute")));
    QVERIFY(fails(QStringLiteral("export_layout"), obj({{QStringLiteral("directory"), dir}, {QStringLiteral("name"), QStringLiteral("../evil")}}))
                .contains(QStringLiteral("plain file name")));
    QVERIFY(fails(QStringLiteral("export_layout"), obj({{QStringLiteral("directory"), dir}, {QStringLiteral("format"), QStringLiteral("docx")}}))
                .contains(QStringLiteral("Unknown format")));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::screenshotReturnsAPng()
{
    const QJsonObject message = obj({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 1},
                                     {QStringLiteral("method"), QStringLiteral("tools/call")},
                                     {QStringLiteral("params"), obj({{QStringLiteral("name"), QStringLiteral("screenshot")},
                                                                     {QStringLiteral("arguments"), obj({{QStringLiteral("max_size"), 300}})}})}});
    const QJsonArray content = rpc(m_port, m_token, message).json().value(QStringLiteral("result")).toObject()
                                   .value(QStringLiteral("content")).toArray();
    QCOMPARE(content.size(), 2);
    const QJsonObject image = content.at(1).toObject();
    QCOMPARE(image.value(QStringLiteral("type")).toString(), QStringLiteral("image"));
    const QByteArray png = QByteArray::fromBase64(image.value(QStringLiteral("data")).toString().toLatin1());
    QVERIFY(png.startsWith("\x89PNG"));
    QVERIFY(png.size() > 200);
    QVERIFY(fails(QStringLiteral("screenshot"), obj({{QStringLiteral("area"), QStringLiteral("nope")}})).contains(QStringLiteral("area")));
}
