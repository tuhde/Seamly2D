# ADD TO EACH PATH $$PWD VARIABLE!!!!!!
# This need for corect working file translations.pro

SOURCES += \
    $$PWD/mcpserver.cpp \
    $$PWD/mcptoolregistry.cpp

*msvc*:SOURCES += $$PWD/stable.cpp

HEADERS += \
    $$PWD/mcpserver.h \
    $$PWD/mcptoolregistry.h \
    $$PWD/stable.h
