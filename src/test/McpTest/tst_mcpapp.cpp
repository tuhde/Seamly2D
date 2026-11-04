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
        "get_pattern_info", "list_objects", "add_draft_block", "add_point", "add_line", "add_curve", "add_arc", "add_elliptical_arc",
        "update_object", "delete_object", "list_variables", "set_variable", "delete_variable", "add_piece",
        "list_pieces", "delete_piece", "update_piece", "set_grainline", "set_notch", "add_anchor_point", "set_measurement", "list_standard_measurements", "create_measurements", "attach_measurements", "undo", "redo", "show_mode", "fit_view", "screenshot", "save_pattern", "open_pattern",
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

    // Changing it so that the lines no longer cross is refused (the program would stop on an error dialog).
    QVERIFY(fails(QStringLiteral("update_object"), obj({{QStringLiteral("id"), x.value(QStringLiteral("id"))}, {QStringLiteral("line2_point1_id"), 1},
                                                       {QStringLiteral("line2_point2_id"), d.value(QStringLiteral("id"))}}))
                .contains(QStringLiteral("parallel")));
    QVERIFY(near(point(QStringLiteral("X")).value(QStringLiteral("x")).toDouble(), m_baseX + 20));

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
void TST_McpApp::undoAndRedoStepBackThroughChanges()
{
    const int before = ok(QStringLiteral("list_objects")).value(QStringLiteral("total")).toInt();
    ok(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("endLine")}, {QStringLiteral("base_point_id"), 1}, {QStringLiteral("length"), QStringLiteral("3")},
                                         {QStringLiteral("angle"), QStringLiteral("45")}, {QStringLiteral("name"), QStringLiteral("Z1")}}));
    QVERIFY(!point(QStringLiteral("Z1")).isEmpty());

    QJsonArray steps = ok(QStringLiteral("undo")).value(QStringLiteral("steps")).toArray();
    QCOMPARE(steps.size(), 1);
    QVERIFY2(steps.at(0).toString().contains(QStringLiteral("Z1")), qPrintable(steps.at(0).toString()));
    QVERIFY(point(QStringLiteral("Z1")).isEmpty());
    QCOMPARE(ok(QStringLiteral("list_objects")).value(QStringLiteral("total")).toInt(), before);

    steps = ok(QStringLiteral("redo")).value(QStringLiteral("steps")).toArray();
    QCOMPARE(steps.size(), 1);
    QVERIFY(!point(QStringLiteral("Z1")).isEmpty());

    // Several steps at once, and the limits of the tool.
    ok(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("endLine")}, {QStringLiteral("base_point_id"), 1}, {QStringLiteral("length"), QStringLiteral("4")},
                                         {QStringLiteral("angle"), QStringLiteral("90")}, {QStringLiteral("name"), QStringLiteral("Z2")}}));
    QCOMPARE(ok(QStringLiteral("undo"), obj({{QStringLiteral("steps"), 2}})).value(QStringLiteral("steps")).toArray().size(), 2);
    QVERIFY(point(QStringLiteral("Z1")).isEmpty() && point(QStringLiteral("Z2")).isEmpty());
    QVERIFY(fails(QStringLiteral("undo"), obj({{QStringLiteral("steps"), 0}})).contains(QStringLiteral("1 to 50")));
    QVERIFY(fails(QStringLiteral("redo"), obj({{QStringLiteral("steps"), 51}})).contains(QStringLiteral("1 to 50")));
    QCOMPARE(ok(QStringLiteral("redo"), obj({{QStringLiteral("steps"), 5}})).value(QStringLiteral("steps")).toArray().size(), 2);   // only two to redo
    QVERIFY(fails(QStringLiteral("redo")).contains(QStringLiteral("nothing to redo")));
    // Leave the pattern as it was for the other tests.
    ok(QStringLiteral("undo"), obj({{QStringLiteral("steps"), 2}}));
    QCOMPARE(ok(QStringLiteral("list_objects")).value(QStringLiteral("total")).toInt(), before);
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
    m_arc = arc.value(QStringLiteral("id")).toInt();

    // An elliptical arc with equal half axes is a circular arc; unequal axes make it longer than the short one.
    const QJsonObject round = ok(QStringLiteral("add_elliptical_arc"), obj({{QStringLiteral("center_point_id"), idA}, {QStringLiteral("radius1"), QStringLiteral("15")}, {QStringLiteral("radius2"), QStringLiteral("15")}, {QStringLiteral("start_angle"), QStringLiteral("0")}, {QStringLiteral("end_angle"), QStringLiteral("90")}}));
    QVERIFY(near(round.value(QStringLiteral("length")).toDouble(), M_PI / 2 * 15, 0.05));
    const QJsonObject oval = ok(QStringLiteral("add_elliptical_arc"), obj({{QStringLiteral("center_point_id"), idA}, {QStringLiteral("radius1"), QStringLiteral("30")}, {QStringLiteral("radius2"), QStringLiteral("15")}, {QStringLiteral("start_angle"), QStringLiteral("0")}, {QStringLiteral("end_angle"), QStringLiteral("90")}, {QStringLiteral("rotation_angle"), QStringLiteral("45")}}));
    QVERIFY(oval.value(QStringLiteral("length")).toDouble() > round.value(QStringLiteral("length")).toDouble());
    QVERIFY(fails(QStringLiteral("add_elliptical_arc"), obj({{QStringLiteral("center_point_id"), idA}, {QStringLiteral("radius1"), QStringLiteral("0")}, {QStringLiteral("radius2"), QStringLiteral("5")}, {QStringLiteral("start_angle"), QStringLiteral("0")}, {QStringLiteral("end_angle"), QStringLiteral("90")}}))
                .contains(QStringLiteral("greater than 0")));

    QVERIFY(fails(QStringLiteral("add_arc"), obj({{QStringLiteral("center_point_id"), idA}, {QStringLiteral("radius"), QStringLiteral("0")},
                                                  {QStringLiteral("start_angle"), QStringLiteral("0")}, {QStringLiteral("end_angle"), QStringLiteral("90")}}))
                .contains(QStringLiteral("greater than 0")));
    fails(QStringLiteral("add_curve"), obj({{QStringLiteral("type"), QStringLiteral("spline")}, {QStringLiteral("start_point_id"), idC},
                                            {QStringLiteral("end_point_id"), idC}, {QStringLiteral("angle1"), QStringLiteral("0")},
                                            {QStringLiteral("angle2"), QStringLiteral("0")}, {QStringLiteral("length1"), QStringLiteral("1")},
                                            {QStringLiteral("length2"), QStringLiteral("1")}}));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::moreConstructionPointsAreComputed()
{
    // The rectangle A(0,0) B(40,0) C(0,20) D(40,20) from the earlier tests, relative to the base point A; M is the
    // middle of A-B; the straight spline C-D is 40 cm long and the arc is a quarter circle of radius 15 around A.
    const int a = 1;
    const int b = point(QStringLiteral("B")).value(QStringLiteral("id")).toInt();
    const int c = point(QStringLiteral("C")).value(QStringLiteral("id")).toInt();
    const int d = point(QStringLiteral("D")).value(QStringLiteral("id")).toInt();
    const int m = point(QStringLiteral("M")).value(QStringLiteral("id")).toInt();
    QVERIFY(m_straightCurve > 0 && m_arc > 0);
    auto add = [this](const QJsonObject &arguments) { return ok(QStringLiteral("add_point"), arguments); };
    auto at = [this](const QJsonObject &p, double x, double y, double tolerance = 0.02)
    {
        const bool good = near(p.value(QStringLiteral("x")).toDouble(), m_baseX + x, tolerance)
                          && near(p.value(QStringLiteral("y")).toDouble(), m_baseY + y, tolerance);
        if (!good)
        {
            qWarning() << "expected" << x << y << "relative to the base point, got"
                       << p.value(QStringLiteral("x")).toDouble() - m_baseX
                       << p.value(QStringLiteral("y")).toDouble() - m_baseY;
        }
        return good;
    };

    // height: foot of the perpendicular from A onto the line B-C.
    QVERIFY(at(add(obj({{QStringLiteral("type"), QStringLiteral("height")}, {QStringLiteral("base_point_id"), a},
                        {QStringLiteral("line_point1_id"), b}, {QStringLiteral("line_point2_id"), c},
                        {QStringLiteral("name"), QStringLiteral("H1")}})), 8, 16));
    // lineIntersectAxis: the axis from A at 315 degrees (down-right) meets the line B-D (x = 40) at y = 40.
    QVERIFY(at(add(obj({{QStringLiteral("type"), QStringLiteral("lineIntersectAxis")}, {QStringLiteral("base_point_id"), a},
                        {QStringLiteral("angle"), QStringLiteral("315")}, {QStringLiteral("first_point_id"), b},
                        {QStringLiteral("second_point_id"), d}, {QStringLiteral("name"), QStringLiteral("L1")}})), 40, 40));
    // curveIntersectAxis: straight down from M meets the spline C-D (y = 20) at x = 20.
    QVERIFY(at(add(obj({{QStringLiteral("type"), QStringLiteral("curveIntersectAxis")}, {QStringLiteral("base_point_id"), m},
                        {QStringLiteral("angle"), QStringLiteral("270")}, {QStringLiteral("curve_id"), m_straightCurve},
                        {QStringLiteral("name"), QStringLiteral("K1")}})), 20, 20));
    // cutCurve: 10 cm from the start / the end of the straight spline, and 10 cm along the quarter circle.
    const QJsonObject cutStart = add(obj({{QStringLiteral("type"), QStringLiteral("cutCurve")}, {QStringLiteral("curve_id"), m_straightCurve},
                                          {QStringLiteral("length"), QStringLiteral("10")}, {QStringLiteral("name"), QStringLiteral("U1")}}));
    QVERIFY(at(cutStart, 10, 20));
    QVERIFY(at(add(obj({{QStringLiteral("type"), QStringLiteral("cutCurve")}, {QStringLiteral("curve_id"), m_straightCurve},
                        {QStringLiteral("length"), QStringLiteral("10")}, {QStringLiteral("from"), QStringLiteral("end")},
                        {QStringLiteral("name"), QStringLiteral("V1")}})), 30, 20));
    QVERIFY(at(add(obj({{QStringLiteral("type"), QStringLiteral("cutCurve")}, {QStringLiteral("curve_id"), m_arc},
                        {QStringLiteral("length"), QStringLiteral("10")}, {QStringLiteral("name"), QStringLiteral("W1")}})),
               15 * qCos(qDegreesToRadians(308.1831)), -15 * qSin(qDegreesToRadians(308.1831))));
    // shoulder: on the line A->B beyond B, at distance sqrt(60^2 + 20^2) from C: the point (60, 0).
    QVERIFY(at(add(obj({{QStringLiteral("type"), QStringLiteral("shoulder")}, {QStringLiteral("line_point1_id"), a},
                        {QStringLiteral("line_point2_id"), b}, {QStringLiteral("shoulder_point_id"), c},
                        {QStringLiteral("length"), QStringLiteral("63.2456")}, {QStringLiteral("name"), QStringLiteral("S1")}})), 60, 0, 0.05));
    // intersectXY: the x of B and the y of C.
    QVERIFY(at(add(obj({{QStringLiteral("type"), QStringLiteral("intersectXY")}, {QStringLiteral("x_from_point_id"), b},
                        {QStringLiteral("y_from_point_id"), c}, {QStringLiteral("name"), QStringLiteral("X1")}})), 40, 20));
    // triangle: the axis y = 10 (E1-F1) crosses the segment A-D at x = 20; the points A and D are seen at a right angle
    // from x = 20 +- sqrt(500), depending on the direction of the axis.
    const int e1 = add(obj({{QStringLiteral("type"), QStringLiteral("endLine")}, {QStringLiteral("base_point_id"), a}, {QStringLiteral("length"), QStringLiteral("10")},
                            {QStringLiteral("angle"), QStringLiteral("270")}, {QStringLiteral("name"), QStringLiteral("E1")}})).value(QStringLiteral("id")).toInt();
    const int f1 = add(obj({{QStringLiteral("type"), QStringLiteral("endLine")}, {QStringLiteral("base_point_id"), b}, {QStringLiteral("length"), QStringLiteral("10")},
                            {QStringLiteral("angle"), QStringLiteral("270")}, {QStringLiteral("name"), QStringLiteral("F1")}})).value(QStringLiteral("id")).toInt();
    QVERIFY(at(add(obj({{QStringLiteral("type"), QStringLiteral("triangle")}, {QStringLiteral("axis_point1_id"), e1}, {QStringLiteral("axis_point2_id"), f1},
                        {QStringLiteral("first_point_id"), a}, {QStringLiteral("second_point_id"), d}, {QStringLiteral("name"), QStringLiteral("T1")}})),
               20 + qSqrt(500.0), 10, 0.1));
    QVERIFY(at(add(obj({{QStringLiteral("type"), QStringLiteral("triangle")}, {QStringLiteral("axis_point1_id"), f1}, {QStringLiteral("axis_point2_id"), e1},
                        {QStringLiteral("first_point_id"), a}, {QStringLiteral("second_point_id"), d}, {QStringLiteral("name"), QStringLiteral("T2")}})),
               20 - qSqrt(500.0), 10, 0.1));

    // update_object refuses a change that leaves the triangle without a solution (axis parallel to the hypotenuse).
    const int t3 = add(obj({{QStringLiteral("type"), QStringLiteral("triangle")}, {QStringLiteral("axis_point1_id"), e1}, {QStringLiteral("axis_point2_id"), f1},
                            {QStringLiteral("first_point_id"), a}, {QStringLiteral("second_point_id"), d}, {QStringLiteral("name"), QStringLiteral("T3")}})).value(QStringLiteral("id")).toInt();
    QVERIFY(fails(QStringLiteral("update_object"), obj({{QStringLiteral("id"), t3}, {QStringLiteral("second_point_id"), b}})).contains(QStringLiteral("no such triangle")));
    QVERIFY(at(point(QStringLiteral("T3")), 20 + qSqrt(500.0), 10, 0.1));

    // Constructions that do not exist are refused before anything is created.
    const int before = ok(QStringLiteral("list_objects")).value(QStringLiteral("total")).toInt();
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("lineIntersectAxis")}, {QStringLiteral("base_point_id"), a},
                                                    {QStringLiteral("angle"), QStringLiteral("0")}, {QStringLiteral("first_point_id"), c},
                                                    {QStringLiteral("second_point_id"), d}})).contains(QStringLiteral("parallel")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("curveIntersectAxis")}, {QStringLiteral("base_point_id"), a},
                                                    {QStringLiteral("angle"), QStringLiteral("0")}, {QStringLiteral("curve_id"), m_straightCurve}}))
                .contains(QStringLiteral("does not cross")));   // the axis y = 0 runs parallel to the spline at y = 20
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("curveIntersectAxis")}, {QStringLiteral("base_point_id"), a},
                                                    {QStringLiteral("angle"), QStringLiteral("0")}, {QStringLiteral("curve_id"), b}})).contains(QStringLiteral("not a curve")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("cutCurve")}, {QStringLiteral("curve_id"), m_straightCurve},
                                                    {QStringLiteral("length"), QStringLiteral("40")}})).contains(QStringLiteral("between 0 and")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("cutCurve")}, {QStringLiteral("curve_id"), m_straightCurve},
                                                    {QStringLiteral("length"), QStringLiteral("5")}, {QStringLiteral("from"), QStringLiteral("middle")}})).contains(QStringLiteral("\"start\" or \"end\"")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("cutCurve")}, {QStringLiteral("curve_id"), b}, {QStringLiteral("length"), QStringLiteral("5")}}))
                .contains(QStringLiteral("not a spline")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("shoulder")}, {QStringLiteral("line_point1_id"), a},
                                                    {QStringLiteral("line_point2_id"), b}, {QStringLiteral("shoulder_point_id"), c},
                                                    {QStringLiteral("length"), QStringLiteral("10")}})).contains(QStringLiteral("no point")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("triangle")}, {QStringLiteral("axis_point1_id"), e1}, {QStringLiteral("axis_point2_id"), f1},
                                                    {QStringLiteral("first_point_id"), a}, {QStringLiteral("second_point_id"), b}})).contains(QStringLiteral("parallel")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("triangle")}, {QStringLiteral("axis_point1_id"), e1}, {QStringLiteral("axis_point2_id"), f1},
                                                    {QStringLiteral("first_point_id"), a}, {QStringLiteral("second_point_id"), e1}})).contains(QStringLiteral("must cross the segment")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("height")}, {QStringLiteral("base_point_id"), a},
                                                    {QStringLiteral("line_point1_id"), c}, {QStringLiteral("line_point2_id"), c}})).contains(QStringLiteral("must differ")));
    QCOMPARE(ok(QStringLiteral("list_objects")).value(QStringLiteral("total")).toInt(), before);

    // They can be changed afterwards like the other points: the cut point follows its length and its direction.
    const int cutId = cutStart.value(QStringLiteral("id")).toInt();
    QVERIFY(at(ok(QStringLiteral("update_object"), obj({{QStringLiteral("id"), cutId}, {QStringLiteral("length"), QStringLiteral("25")}})), 25, 20));
    QVERIFY(at(ok(QStringLiteral("update_object"), obj({{QStringLiteral("id"), cutId}, {QStringLiteral("from"), QStringLiteral("end")}})), 15, 20));
    QVERIFY(fails(QStringLiteral("update_object"), obj({{QStringLiteral("id"), cutId}, {QStringLiteral("from"), QStringLiteral("middle")}})).contains(QStringLiteral("\"start\" or \"end\"")));
    QVERIFY(fails(QStringLiteral("update_object"), obj({{QStringLiteral("id"), cutId}, {QStringLiteral("curve_id"), b}})).contains(QStringLiteral("curve or arc")));
    ok(QStringLiteral("update_object"), obj({{QStringLiteral("id"), cutId}, {QStringLiteral("length"), QStringLiteral("10")}, {QStringLiteral("from"), QStringLiteral("start")}}));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::pathCurvesRunSmoothlyThroughPoints()
{
    const int a = 1;
    const int b = point(QStringLiteral("B")).value(QStringLiteral("id")).toInt();
    const int m = point(QStringLiteral("M")).value(QStringLiteral("id")).toInt();
    auto addPoint = [this](const QString &name, int base, const QString &length, const QString &angle)
    {
        return ok(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("endLine")}, {QStringLiteral("base_point_id"), base},
                                                    {QStringLiteral("length"), length}, {QStringLiteral("angle"), angle}, {QStringLiteral("name"), name}}))
            .value(QStringLiteral("id")).toInt();
    };

    // Three collinear points give a straight line: A, M (middle of A-B) and B, 40 cm.
    const QJsonObject straight = ok(QStringLiteral("add_curve"), obj({{QStringLiteral("type"), QStringLiteral("path")}, {QStringLiteral("points"), QJsonArray{a, m, b}}}));
    QCOMPARE(straight.value(QStringLiteral("kind")).toString(), QStringLiteral("path"));
    QCOMPARE(straight.value(QStringLiteral("points")).toInt(), 3);
    QVERIFY2(near(straight.value(QStringLiteral("length")).toDouble(), 40.0, 0.05), qPrintable(QJsonDocument(straight).toJson()));

    // A wave through four points is smooth and longer than the chord.
    const int p2 = addPoint(QStringLiteral("PP2"), a, QStringLiteral("15"), QStringLiteral("20"));
    const int p3 = addPoint(QStringLiteral("PP3"), a, QStringLiteral("30"), QStringLiteral("340"));
    const int p4 = addPoint(QStringLiteral("PP4"), a, QStringLiteral("45"), QStringLiteral("10"));
    const QJsonObject wave = ok(QStringLiteral("add_curve"), obj({{QStringLiteral("type"), QStringLiteral("path")}, {QStringLiteral("points"), QJsonArray{a, p2, p3, p4}}}));
    QVERIFY(wave.value(QStringLiteral("length")).toDouble() > 45.0);
    QVERIFY(wave.value(QStringLiteral("length")).toDouble() < 80.0);

    // Per-point control: a bigger handle at PP3 changes the shape (and so the length).
    const QJsonObject shaped = ok(QStringLiteral("add_curve"),
                                  obj({{QStringLiteral("type"), QStringLiteral("path")},
                                       {QStringLiteral("points"), QJsonArray{a, p2, obj({{QStringLiteral("point_id"), p3}, {QStringLiteral("angle"), QStringLiteral("0")},
                                                                                         {QStringLiteral("length_in"), QStringLiteral("12")}, {QStringLiteral("length_out"), QStringLiteral("12")}}),
                                                                             p4}}}));
    QVERIFY(!near(shaped.value(QStringLiteral("length")).toDouble(), wave.value(QStringLiteral("length")).toDouble(), 0.5));

    // The points of the path are visible in list_objects.
    const QJsonArray splines = objects(obj({{QStringLiteral("ids"), QJsonArray{wave.value(QStringLiteral("id"))}}}));
    QCOMPARE(splines.size(), 1);
    const QJsonArray children = splines.at(0).toObject().value(QStringLiteral("children")).toArray();
    QCOMPARE(children.size(), 4);
    QCOMPARE(children.at(0).toObject().value(QStringLiteral("pSpline")).toString().toInt(), a);
    QCOMPARE(children.at(3).toObject().value(QStringLiteral("pSpline")).toString().toInt(), p4);

    // Errors leave everything unchanged.
    const int before = ok(QStringLiteral("list_objects")).value(QStringLiteral("total")).toInt();
    QVERIFY(fails(QStringLiteral("add_curve"), obj({{QStringLiteral("type"), QStringLiteral("path")}, {QStringLiteral("points"), QJsonArray{a, m}}})).contains(QStringLiteral("at least 3")));
    QVERIFY(fails(QStringLiteral("add_curve"), obj({{QStringLiteral("type"), QStringLiteral("path")}, {QStringLiteral("points"), QJsonArray{a, m, m}}})).contains(QStringLiteral("twice")));
    QVERIFY(fails(QStringLiteral("add_curve"), obj({{QStringLiteral("type"), QStringLiteral("path")}, {QStringLiteral("points"), QJsonArray{a, m, 9999}}})).contains(QStringLiteral("does not exist")));
    QVERIFY(fails(QStringLiteral("add_curve"), obj({{QStringLiteral("type"), QStringLiteral("path")}, {QStringLiteral("points"), QJsonArray{a, m, obj({{QStringLiteral("point_id"), b}, {QStringLiteral("angle"), QStringLiteral("x+")}})}}})).contains(QStringLiteral("Invalid formula")));
    QVERIFY(fails(QStringLiteral("add_curve"), obj({{QStringLiteral("type"), QStringLiteral("path")}, {QStringLiteral("points"), QJsonArray{a, m, obj({{QStringLiteral("point_id"), b}, {QStringLiteral("length_in"), QStringLiteral("-3")}})}}})).contains(QStringLiteral("negative")));
    QCOMPARE(ok(QStringLiteral("list_objects")).value(QStringLiteral("total")).toInt(), before);

    // A path can be deleted like any other object.
    ok(QStringLiteral("delete_object"), obj({{QStringLiteral("id"), shaped.value(QStringLiteral("id"))}}));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::circleArcAndCurveIntersectionsAreComputed()
{
    const int a = 1;
    const int b = point(QStringLiteral("B")).value(QStringLiteral("id")).toInt();
    const int m = point(QStringLiteral("M")).value(QStringLiteral("id")).toInt();
    auto add = [this](const QJsonObject &arguments) { return ok(QStringLiteral("add_point"), arguments); };
    auto rel = [this](const QJsonObject &p) { return QPointF(p.value(QStringLiteral("x")).toDouble() - m_baseX, p.value(QStringLiteral("y")).toDouble() - m_baseY); };
    auto isAt = [](const QPointF &p, double x, double y, double tolerance = 0.02) { return near(p.x(), x, tolerance) && near(p.y(), y, tolerance); };

    // Two circles of radius 30 around A and B meet at x = 20, y = +-sqrt(500); the two choices are the two points.
    const QPointF first = rel(add(obj({{QStringLiteral("type"), QStringLiteral("intersectCircles")}, {QStringLiteral("center1_point_id"), a}, {QStringLiteral("radius1"), QStringLiteral("30")},
                                       {QStringLiteral("center2_point_id"), b}, {QStringLiteral("radius2"), QStringLiteral("30")}, {QStringLiteral("name"), QStringLiteral("CI1")}})));
    const QPointF second = rel(add(obj({{QStringLiteral("type"), QStringLiteral("intersectCircles")}, {QStringLiteral("center1_point_id"), a}, {QStringLiteral("radius1"), QStringLiteral("30")},
                                        {QStringLiteral("center2_point_id"), b}, {QStringLiteral("radius2"), QStringLiteral("30")}, {QStringLiteral("crossing"), QStringLiteral("second")},
                                        {QStringLiteral("name"), QStringLiteral("CI2")}})));
    QVERIFY(near(first.x(), 20) && near(second.x(), 20));
    QVERIFY(near(qAbs(first.y()), qSqrt(500.0)) && near(qAbs(second.y()), qSqrt(500.0)));
    QVERIFY(first.y() * second.y() < 0);

    // A circle of radius 25 around A meets the vertical segment x = 20 (from M down 40 cm) at y = 15.
    const int n2 = add(obj({{QStringLiteral("type"), QStringLiteral("endLine")}, {QStringLiteral("base_point_id"), m}, {QStringLiteral("length"), QStringLiteral("40")},
                            {QStringLiteral("angle"), QStringLiteral("270")}, {QStringLiteral("name"), QStringLiteral("N2")}})).value(QStringLiteral("id")).toInt();
    QVERIFY(isAt(rel(add(obj({{QStringLiteral("type"), QStringLiteral("intersectCircleLine")}, {QStringLiteral("center_point_id"), a}, {QStringLiteral("radius"), QStringLiteral("25")},
                              {QStringLiteral("line_point1_id"), m}, {QStringLiteral("line_point2_id"), n2}, {QStringLiteral("name"), QStringLiteral("CL1")}}))), 20, 15));

    // Tangents from B (40, 0) to the circle of radius 10 around A touch it at (2.5, +-sqrt(93.75)).
    const QPointF t1 = rel(add(obj({{QStringLiteral("type"), QStringLiteral("tangentToCircle")}, {QStringLiteral("center_point_id"), a}, {QStringLiteral("radius"), QStringLiteral("10")},
                                    {QStringLiteral("tangent_point_id"), b}, {QStringLiteral("name"), QStringLiteral("TC1")}})));
    const QPointF t2 = rel(add(obj({{QStringLiteral("type"), QStringLiteral("tangentToCircle")}, {QStringLiteral("center_point_id"), a}, {QStringLiteral("radius"), QStringLiteral("10")},
                                    {QStringLiteral("tangent_point_id"), b}, {QStringLiteral("crossing"), QStringLiteral("second")}, {QStringLiteral("name"), QStringLiteral("TC2")}})));
    QVERIFY(near(t1.x(), 2.5, 0.02) && near(t2.x(), 2.5, 0.02));
    QVERIFY(near(qAbs(t1.y()), qSqrt(93.75), 0.02) && t1.y() * t2.y() < 0);

    // Arcs: the quarter circles around A (270-360) and B (180-270), radius 30, meet at (20, sqrt(500)); the other
    // intersection of their circles is not on the arcs.
    const int arcA = ok(QStringLiteral("add_arc"), obj({{QStringLiteral("center_point_id"), a}, {QStringLiteral("radius"), QStringLiteral("30")}, {QStringLiteral("start_angle"), QStringLiteral("270")}, {QStringLiteral("end_angle"), QStringLiteral("360")}})).value(QStringLiteral("id")).toInt();
    const int arcB = ok(QStringLiteral("add_arc"), obj({{QStringLiteral("center_point_id"), b}, {QStringLiteral("radius"), QStringLiteral("30")}, {QStringLiteral("start_angle"), QStringLiteral("180")}, {QStringLiteral("end_angle"), QStringLiteral("270")}})).value(QStringLiteral("id")).toInt();
    QVERIFY(isAt(rel(add(obj({{QStringLiteral("type"), QStringLiteral("intersectArcs")}, {QStringLiteral("first_arc_id"), arcA}, {QStringLiteral("second_arc_id"), arcB}, {QStringLiteral("name"), QStringLiteral("AI1")}}))),
                 20, qSqrt(500.0)));
    // Tangent from B to the arc of radius 10 around A: on the circle and perpendicular to the tangent line.
    const int arcC = ok(QStringLiteral("add_arc"), obj({{QStringLiteral("center_point_id"), a}, {QStringLiteral("radius"), QStringLiteral("10")}, {QStringLiteral("start_angle"), QStringLiteral("270")}, {QStringLiteral("end_angle"), QStringLiteral("360")}})).value(QStringLiteral("id")).toInt();
    const QPointF ta = rel(add(obj({{QStringLiteral("type"), QStringLiteral("tangentToArc")}, {QStringLiteral("arc_id"), arcC}, {QStringLiteral("tangent_point_id"), b}, {QStringLiteral("name"), QStringLiteral("TA1")}})));
    QVERIFY(near(qHypot(ta.x(), ta.y()), 10.0, 0.02));
    QVERIFY(near(ta.x() * (ta.x() - 40) + ta.y() * ta.y(), 0.0, 0.1));

    // Two curves: the straight spline C-D (y = 20) and a diagonal from A to (40, 40) cross at (20, 20).
    const int l2 = add(obj({{QStringLiteral("type"), QStringLiteral("endLine")}, {QStringLiteral("base_point_id"), b}, {QStringLiteral("length"), QStringLiteral("40")},
                            {QStringLiteral("angle"), QStringLiteral("270")}, {QStringLiteral("name"), QStringLiteral("L2")}})).value(QStringLiteral("id")).toInt();
    const int diagonal = ok(QStringLiteral("add_curve"), obj({{QStringLiteral("type"), QStringLiteral("spline")}, {QStringLiteral("start_point_id"), a}, {QStringLiteral("end_point_id"), l2},
                                                              {QStringLiteral("angle1"), QStringLiteral("315")}, {QStringLiteral("angle2"), QStringLiteral("135")},
                                                              {QStringLiteral("length1"), QStringLiteral("10")}, {QStringLiteral("length2"), QStringLiteral("10")}})).value(QStringLiteral("id")).toInt();
    QVERIFY(isAt(rel(add(obj({{QStringLiteral("type"), QStringLiteral("intersectCurves")}, {QStringLiteral("first_curve_id"), m_straightCurve}, {QStringLiteral("second_curve_id"), diagonal},
                              {QStringLiteral("name"), QStringLiteral("CC1")}}))), 20, 20));

    // update_object works on the circle/arc/curve intersection points, too: the point follows the new radius.
    const int ic = add(obj({{QStringLiteral("type"), QStringLiteral("intersectCircles")}, {QStringLiteral("center1_point_id"), a}, {QStringLiteral("radius1"), QStringLiteral("30")},
                            {QStringLiteral("center2_point_id"), b}, {QStringLiteral("radius2"), QStringLiteral("30")}, {QStringLiteral("name"), QStringLiteral("IC")}})).value(QStringLiteral("id")).toInt();
    QVERIFY(near(rel(ok(QStringLiteral("update_object"), obj({{QStringLiteral("id"), ic}, {QStringLiteral("radius1"), QStringLiteral("25")}}))).x(), 16.5625, 0.02));
    QVERIFY(fails(QStringLiteral("update_object"), obj({{QStringLiteral("id"), ic}, {QStringLiteral("radius1"), QStringLiteral("5")}})).contains(QStringLiteral("would not intersect")));
    QVERIFY(near(point(QStringLiteral("IC")).value(QStringLiteral("x")).toDouble() - m_baseX, 16.5625, 0.02));   // unchanged
    QVERIFY(fails(QStringLiteral("update_object"), obj({{QStringLiteral("id"), ic}, {QStringLiteral("radius1"), QStringLiteral("nonsense+")}})).contains(QStringLiteral("radius1")));
    QVERIFY(fails(QStringLiteral("update_object"), obj({{QStringLiteral("id"), ic}, {QStringLiteral("center1_point_id"), ic}})).contains(QStringLiteral("itself")));

    // No intersection, no result.
    const int before = ok(QStringLiteral("list_objects")).value(QStringLiteral("total")).toInt();
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("intersectCircles")}, {QStringLiteral("center1_point_id"), a}, {QStringLiteral("radius1"), QStringLiteral("5")},
                                                    {QStringLiteral("center2_point_id"), b}, {QStringLiteral("radius2"), QStringLiteral("5")}})).contains(QStringLiteral("do not intersect")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("intersectCircles")}, {QStringLiteral("center1_point_id"), a}, {QStringLiteral("radius1"), QStringLiteral("30")},
                                                    {QStringLiteral("center2_point_id"), a}, {QStringLiteral("radius2"), QStringLiteral("30")}})).contains(QStringLiteral("different centers")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("intersectCircles")}, {QStringLiteral("center1_point_id"), a}, {QStringLiteral("radius1"), QStringLiteral("30")},
                                                    {QStringLiteral("center2_point_id"), b}, {QStringLiteral("radius2"), QStringLiteral("30")}, {QStringLiteral("crossing"), QStringLiteral("third")}})).contains(QStringLiteral("\"first\" or \"second\"")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("intersectCircleLine")}, {QStringLiteral("center_point_id"), a}, {QStringLiteral("radius"), QStringLiteral("5")},
                                                    {QStringLiteral("line_point1_id"), m}, {QStringLiteral("line_point2_id"), n2}})).contains(QStringLiteral("do not meet")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("tangentToCircle")}, {QStringLiteral("center_point_id"), a}, {QStringLiteral("radius"), QStringLiteral("50")},
                                                    {QStringLiteral("tangent_point_id"), b}})).contains(QStringLiteral("inside the circle")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("intersectArcs")}, {QStringLiteral("first_arc_id"), arcA}, {QStringLiteral("second_arc_id"), arcA}})).contains(QStringLiteral("two different arcs")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("intersectArcs")}, {QStringLiteral("first_arc_id"), arcA}, {QStringLiteral("second_arc_id"), b}})).contains(QStringLiteral("two different arcs")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("tangentToArc")}, {QStringLiteral("arc_id"), b}, {QStringLiteral("tangent_point_id"), a}})).contains(QStringLiteral("not an arc")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("intersectCurves")}, {QStringLiteral("first_curve_id"), m_straightCurve}, {QStringLiteral("second_curve_id"), m_straightCurve}})).contains(QStringLiteral("two different curves")));
    QVERIFY(fails(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("intersectCurves")}, {QStringLiteral("first_curve_id"), m_straightCurve}, {QStringLiteral("second_curve_id"), diagonal},
                                                    {QStringLiteral("vertical"), QStringLiteral("middle")}})).contains(QStringLiteral("\"highest\" or \"lowest\"")));
    QCOMPARE(ok(QStringLiteral("list_objects")).value(QStringLiteral("total")).toInt(), before);
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

    // Anchor points: any point of the drawing; twice is refused; Undo takes it away again.
    QCOMPARE(ok(QStringLiteral("add_anchor_point"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), pointM}})).value(QStringLiteral("anchorPoints")).toInt(), 1);
    QVERIFY(fails(QStringLiteral("add_anchor_point"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), pointM}})).contains(QStringLiteral("already")));
    QVERIFY(fails(QStringLiteral("add_anchor_point"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), 99999}})).contains(QStringLiteral("point_id")));
    const QJsonArray listedAnchors = ok(QStringLiteral("list_pieces")).value(QStringLiteral("pieces")).toArray().at(0).toObject().value(QStringLiteral("anchorPoints")).toArray();
    QCOMPARE(listedAnchors.size(), 1);
    QCOMPARE(listedAnchors.at(0).toObject().value(QStringLiteral("pointId")).toInt(), pointM);
    QVERIFY(ok(QStringLiteral("undo")).value(QStringLiteral("steps")).toArray().at(0).toString().contains(QStringLiteral("anchor")));
    QCOMPARE(ok(QStringLiteral("add_anchor_point"), obj({{QStringLiteral("piece_id"), id}, {QStringLiteral("point_id"), pointM}})).value(QStringLiteral("anchorPoints")).toInt(), 1);
    ok(QStringLiteral("undo"));

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

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::measurementValuesCanBeChanged()
{
    // A pattern that comes with an individual measurement file: the trousers of the other test projects. They are
    // copied (the measurement path is rewritten to the copy), because the test changes the measurement file.
    const QString source = QString::fromUtf8(SRCDIR) + QLatin1String("../CollectionTest/share/trousers/");
    const QString folder = m_workDir.path() + QLatin1String("/meas");
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(QFile::copy(source + QLatin1String("trousers_v0_6_8.smis"), folder + QLatin1String("/trousers.smis")));
    QFile pattern(source + QLatin1String("trousers_v0_6_8.sm2d"));
    QVERIFY(pattern.open(QIODevice::ReadOnly));
    QString xml = QString::fromUtf8(pattern.readAll());
    xml.replace(QRegularExpression(QStringLiteral("<measurements>[^<]*</measurements>")),
                QStringLiteral("<measurements>trousers.smis</measurements>"));
    QFile copy(folder + QLatin1String("/trousers.sm2d"));
    QVERIFY(copy.open(QIODevice::WriteOnly));
    copy.write(xml.toUtf8());
    copy.close();
    const QString measurementFile = folder + QLatin1String("/trousers.smis");
    auto fileValue = [&measurementFile]()
    {
        QFile f(measurementFile);
        if (!f.open(QIODevice::ReadOnly))
        {
            return QString();
        }
        const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("name=\"waist_circ\"[^>]*value=\"([^\"]*)\"|value=\"([^\"]*)\"[^>]*name=\"waist_circ\""))
                                                  .match(QString::fromUtf8(f.readAll()));
        return match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
    };
    auto measurement = [this](const QString &name)
    {
        for (const QJsonValue &m : ok(QStringLiteral("list_variables")).value(QStringLiteral("measurements")).toArray())
        {
            if (m.toObject().value(QStringLiteral("name")).toString() == name)
            {
                return m.toObject().value(QStringLiteral("value")).toDouble(-1);
            }
        }
        return -1.0;
    };
    auto variable = [this](const QString &name)
    {
        for (const QJsonValue &v : ok(QStringLiteral("list_variables")).value(QStringLiteral("variables")).toArray())
        {
            if (v.toObject().value(QStringLiteral("name")).toString() == name)
            {
                return v.toObject().value(QStringLiteral("value")).toDouble(-1);
            }
        }
        return -1.0;
    };

    stopApp();
    QVERIFY(startApp());
    // Without a measurement file there is nothing to change.
    QVERIFY(fails(QStringLiteral("set_measurement"), obj({{QStringLiteral("name"), QStringLiteral("waist_circ")}, {QStringLiteral("value"), 90}}))
                .contains(QStringLiteral("no measurement")));
    ok(QStringLiteral("open_pattern"), obj({{QStringLiteral("path"), folder + QLatin1String("/trousers.sm2d")}}));

    // waist_circ is 84 and WaistCircumference (= waist_circ + 2) is 86.
    QCOMPARE(measurement(QStringLiteral("waist_circ")), 84.0);
    QCOMPARE(variable(QStringLiteral("WaistCircumference")), 86.0);
    QCOMPARE(fileValue(), QStringLiteral("84"));

    const QJsonObject changed = ok(QStringLiteral("set_measurement"), obj({{QStringLiteral("name"), QStringLiteral("waist_circ")}, {QStringLiteral("value"), 90}}));
    QCOMPARE(changed.value(QStringLiteral("oldValue")).toDouble(), 84.0);
    QCOMPARE(changed.value(QStringLiteral("value")).toDouble(), 90.0);
    QCOMPARE(measurement(QStringLiteral("waist_circ")), 90.0);
    QCOMPARE(variable(QStringLiteral("WaistCircumference")), 92.0);   // the formulas that use it follow
    QCOMPARE(fileValue(), QStringLiteral("90"));                      // and it really is in the file

    // Undo writes the old value back to the file, redo writes the new one again.
    QVERIFY(ok(QStringLiteral("undo")).value(QStringLiteral("steps")).toArray().at(0).toString().contains(QStringLiteral("waist_circ")));
    QCOMPARE(measurement(QStringLiteral("waist_circ")), 84.0);
    QCOMPARE(variable(QStringLiteral("WaistCircumference")), 86.0);
    QCOMPARE(fileValue(), QStringLiteral("84"));
    ok(QStringLiteral("redo"));
    QCOMPARE(measurement(QStringLiteral("waist_circ")), 90.0);
    QCOMPARE(fileValue(), QStringLiteral("90"));
    ok(QStringLiteral("undo"));
    QCOMPARE(fileValue(), QStringLiteral("84"));

    // Refused: unknown names, things that are not measurements, bad values.
    QVERIFY(fails(QStringLiteral("set_measurement"), obj({{QStringLiteral("name"), QStringLiteral("no_such")}, {QStringLiteral("value"), 1}})).contains(QStringLiteral("no measurement named")));
    QVERIFY(fails(QStringLiteral("set_measurement"), obj({{QStringLiteral("name"), QStringLiteral("WaistCircumference")}, {QStringLiteral("value"), 1}})).contains(QStringLiteral("no measurement named")));
    QVERIFY(fails(QStringLiteral("set_measurement"), obj({{QStringLiteral("name"), QStringLiteral("waist_circ")}, {QStringLiteral("value"), -1}})).contains(QStringLiteral("0 to 100000")));
    QVERIFY(fails(QStringLiteral("set_measurement"), obj({{QStringLiteral("name"), QStringLiteral("waist_circ")}})).contains(QStringLiteral("required")));
    QCOMPARE(measurement(QStringLiteral("waist_circ")), 84.0);
    QCOMPARE(fileValue(), QStringLiteral("84"));
}

//---------------------------------------------------------------------------------------------------------------------
void TST_McpApp::measurementFileCanBeCreatedAndAttached()
{
    const QString folder = m_workDir.path() + QLatin1String("/newmeas");
    QVERIFY(QDir().mkpath(folder));
    const QString file = folder + QLatin1String("/anna.smis");

    stopApp();
    QVERIFY(startApp());

    // The standard names can be searched.
    const QJsonObject found = ok(QStringLiteral("list_standard_measurements"), obj({{QStringLiteral("query"), QStringLiteral("waist_circ")}}));
    QVERIFY(found.value(QStringLiteral("matches")).toInt() >= 1);
    QCOMPARE(found.value(QStringLiteral("measurements")).toArray().at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("waist_circ"));

    // Refused: unknown names, bad values, relative paths, wrong suffix; nothing is written.
    QVERIFY(fails(QStringLiteral("create_measurements"), obj({{QStringLiteral("path"), file}, {QStringLiteral("values"), obj({{QStringLiteral("no_such_name"), 1}})}}))
                .contains(QStringLiteral("not a standard measurement")));
    QVERIFY(fails(QStringLiteral("create_measurements"), obj({{QStringLiteral("path"), file}, {QStringLiteral("values"), obj({{QStringLiteral("waist_circ"), -5}})}}))
                .contains(QStringLiteral("0 to 100000")));
    QVERIFY(fails(QStringLiteral("create_measurements"), obj({{QStringLiteral("path"), QStringLiteral("anna.smis")}, {QStringLiteral("values"), obj({{QStringLiteral("waist_circ"), 80}})}}))
                .contains(QStringLiteral("absolute")));
    QVERIFY(fails(QStringLiteral("create_measurements"), obj({{QStringLiteral("path"), folder + QLatin1String("/anna.txt")}, {QStringLiteral("values"), obj({{QStringLiteral("waist_circ"), 80}})}}))
                .contains(QStringLiteral(".smis")));
    QVERIFY(!QFileInfo::exists(file));

    ok(QStringLiteral("create_measurements"),
       obj({{QStringLiteral("path"), file}, {QStringLiteral("unit"), QStringLiteral("cm")},
            {QStringLiteral("values"), obj({{QStringLiteral("waist_circ"), 80}, {QStringLiteral("hip_circ"), 98}})}}));
    QVERIFY(QFileInfo::exists(file));
    // An existing file is never overwritten.
    QVERIFY(fails(QStringLiteral("create_measurements"), obj({{QStringLiteral("path"), file}, {QStringLiteral("values"), obj({{QStringLiteral("waist_circ"), 1}})}}))
                .contains(QStringLiteral("already exists")));

    // Attaching needs a draft block.
    QVERIFY(fails(QStringLiteral("attach_measurements"), obj({{QStringLiteral("path"), file}})).contains(QStringLiteral("draft block")));
    const QJsonObject block = ok(QStringLiteral("add_draft_block"), obj({{QStringLiteral("unit"), QStringLiteral("cm")}}));
    const double baseX = block.value(QStringLiteral("basePoint")).toObject().value(QStringLiteral("x")).toDouble();
    QVERIFY(fails(QStringLiteral("attach_measurements"), obj({{QStringLiteral("path"), folder + QLatin1String("/missing.smis")}})).contains(QStringLiteral("existing")));
    ok(QStringLiteral("attach_measurements"), obj({{QStringLiteral("path"), file}}));

    double waist = -1;
    for (const QJsonValue &m : ok(QStringLiteral("list_variables")).value(QStringLiteral("measurements")).toArray())
    {
        if (m.toObject().value(QStringLiteral("name")).toString() == QLatin1String("waist_circ"))
        {
            waist = m.toObject().value(QStringLiteral("value")).toDouble(-1);
        }
    }
    QCOMPARE(waist, 80.0);

    // The measurement can be used in a formula, and changed afterwards.
    const QJsonObject b = ok(QStringLiteral("add_point"), obj({{QStringLiteral("type"), QStringLiteral("endLine")}, {QStringLiteral("base_point_id"), 1}, {QStringLiteral("length"), QStringLiteral("waist_circ / 4")}, {QStringLiteral("angle"), QStringLiteral("0")}, {QStringLiteral("name"), QStringLiteral("B")}}));
    QVERIFY(qAbs(b.value(QStringLiteral("x")).toDouble() - (baseX + 20)) < 0.01);   // waist_circ / 4 = 20 cm
    ok(QStringLiteral("set_measurement"), obj({{QStringLiteral("name"), QStringLiteral("waist_circ")}, {QStringLiteral("value"), 88}}));
    QVERIFY(qAbs(point(QStringLiteral("B")).value(QStringLiteral("x")).toDouble() - (baseX + 22)) < 0.01);
}
