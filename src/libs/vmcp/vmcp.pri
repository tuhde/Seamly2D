# ADD TO EACH PATH $$PWD VARIABLE!!!!!!
# This need for corect working file translations.pro

SOURCES += \
    $$PWD/mcpinstances.cpp \
    $$PWD/mcprouter.cpp \
    $$PWD/mcpserver.cpp \
    $$PWD/mcptoolregistry.cpp

*msvc*:SOURCES += $$PWD/stable.cpp

HEADERS += \
    $$PWD/mcpinstances.h \
    $$PWD/mcprouter.h \
    $$PWD/mcpserver.h \
    $$PWD/mcptoolregistry.h \
    $$PWD/stable.h
