# for use valgrid: export LIBGL_ALWAYS_SOFTWARE=1
# for use perf: sudo sysctl -w kernel.perf_event_paranoid=1
QT = core network sql httpserver

TARGET = TradingCat
TEMPLATE = app

CONFIG += c++20 cmdline
CONFIG += static

VERSION = 0.2

HEADERS += \
    $$PWD/Src/appserver.h \
    $$PWD/Src/config.h \
    $$PWD/Src/core.h \
    $$PWD/Src/userscore.h \
    $$PWD/Src/usersdata.h

SOURCES += \
    $$PWD/Src/appserver.cpp \
    $$PWD/Src/config.cpp \
    $$PWD/Src/core.cpp \
    $$PWD/Src/main.cpp \
    $$PWD/Src/userscore.cpp \
    $$PWD/Src/usersdata.cpp

#inlude addition library
include($$PWD/../../Common/Common/Common.pri)
include($$PWD/../TradingCatCommon/TradingCatCommon.pri)
include($$PWD/../StockExchange/StockExchange.pri)
