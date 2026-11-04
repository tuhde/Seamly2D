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
#ifndef TST_MCPSERVER_H
#define TST_MCPSERVER_H

#include <QObject>

#include "../../libs/vmcp/mcpserver.h"
#include "../../libs/vmcp/mcptoolregistry.h"

/// Tests of the protocol library: JSON-RPC handling and the HTTP layer (auth, DNS-rebinding checks, limits).
class TST_McpServer : public QObject
{
    Q_OBJECT
public:
    TST_McpServer() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();

    // JSON-RPC (no sockets)
    void initializeNegotiatesTheVersion();
    void initializeReportsInstructionsAndTools();
    void toolsListDescribesTheTools();
    void toolCallReturnsTextAndStructuredContent();
    void toolErrorIsAResultNotAProtocolError();
    void toolCallProblemsAreProtocolErrors();
    void imageResultIsReturnedAsImageContent();
    void notificationsGetNoReply();
    void pingAnswers();
    void wrongJsonRpcVersionIsRejected();

    // HTTP layer
    void rejectsMissingOrWrongToken();
    void acceptsTheTokenCaseInsensitivelyInTheScheme();
    void rejectsForeignHostAndOrigin();
    void onlyPostToTheEndpointIsAllowed();
    void rejectsMalformedJson();
    void rejectsOversizedBodies();
    void notificationsOverHttpAreAccepted();
    void batchesAreAnsweredOneByOne();
    void aSecondRequestWhileAToolRunsIsRefused();
    void newTokenLocksOutTheOldOne();

private:
    QJsonObject call(const QString &method, const QJsonObject &params = QJsonObject(), int id = 1);

    McpToolRegistry m_registry;
    McpServer      *m_server = nullptr;
    QString         m_token;
};

#endif // TST_MCPSERVER_H
