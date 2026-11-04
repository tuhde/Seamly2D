#-------------------------------------------------
# Tests for the MCP server (Claude connection):
#  - TST_McpServer: protocol library (vmcp), no GUI
#  - TST_McpApp:    starts the real program (needs a display, CI uses xvfb-run) and drives the pattern tools through
#                   JSON-RPC over HTTP. Skipped when the program binary has not been built or there is no display.
#-------------------------------------------------

message("Entering McpTest.pro")
include(../../../common.pri)

QT       += core network testlib
QT       -= gui

TARGET = McpTests

# CONFIG += testcase adds 'make check'. 'no_testcase_installs' keeps it from installing the tests.
CONFIG += testcase no_testcase_installs
CONFIG -= app_bundle

# No precompiled header for this small project (common.pri would look for a stable.h here).
CONFIG -= precompile_header
PRECOMPILED_HEADER =

DESTDIR = bin
MOC_DIR = moc
OBJECTS_DIR = obj

SOURCES += \
    main.cpp \
    tst_mcpserver.cpp \
    tst_mcpapp.cpp

HEADERS += \
    mcptestclient.h \
    tst_mcpserver.h \
    tst_mcpapp.h

include(warnings.pri)

# VMcp static library (protocol library, Qt core + network only)
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vmcp/$${DESTDIR}/ -lvmcp

INCLUDEPATH += $$PWD/../../libs/vmcp
DEPENDPATH += $$PWD/../../libs/vmcp

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vmcp/$${DESTDIR}/vmcp.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vmcp/$${DESTDIR}/libvmcp.a
