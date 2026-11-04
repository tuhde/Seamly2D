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
#include <QCoreApplication>
#include <QtTest>

#include "tst_mcpapp.h"
#include "tst_mcpserver.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    int status = 0;
    {
        TST_McpServer test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        TST_McpApp test;
        status |= QTest::qExec(&test, argc, argv);
    }
    return status;
}
