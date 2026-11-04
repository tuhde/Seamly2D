#-------------------------------------------------
# vmcp: Model Context Protocol server (JSON-RPC 2.0 over local HTTP).
# Protocol only: depends on Qt core + network, not on the pattern libraries.
#-------------------------------------------------

message("Entering vmcp.pro")
include(../../../common.pri)

QT       += network
QT       -= gui

TARGET = vmcp
TEMPLATE = lib
CONFIG += staticlib

include(vmcp.pri)

# This is static library so no need in "make install"

# directory for executable file
DESTDIR = bin

# files created moc
MOC_DIR = moc

# objecs files
OBJECTS_DIR = obj

include(warnings.pri)

include (../libs.pri)
