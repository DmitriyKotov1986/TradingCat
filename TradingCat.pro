# for use valgrid: export LIBGL_ALWAYS_SOFTWARE=1
QT = core network sql httpserver

TARGET = TradingCat
TEMPLATE = app

CONFIG += c++20 cmdline
CONFIG += static

VERSION = 0.1

HEADERS += \
    $$PWD/Src/appserver.h \
    $$PWD/Src/binance.h \
    $$PWD/Src/binancekline.h \
    $$PWD/Src/bingx.h \
    $$PWD/Src/bingxkline.h \
    $$PWD/Src/bitget.h \
    $$PWD/Src/bitgetkline.h \
    $$PWD/Src/bybit.h \
    $$PWD/Src/bybitkline.h \
    $$PWD/Src/config.h \
    $$PWD/Src/core.h \
    $$PWD/Src/gate.h \
    $$PWD/Src/gatekline.h \
    $$PWD/Src/kucoin.h \
    $$PWD/Src/kucoinkline.h \
    $$PWD/Src/mexc.h \
    $$PWD/Src/mexckline.h \
    $$PWD/Src/moex.h \
    $$PWD/Src/moexkline.h \
    $$PWD/Src/okx.h \
    $$PWD/Src/okxkline.h \
    $$PWD/Src/userscore.h \
    $$PWD/Src/usersdata.h

SOURCES += \
    $$PWD/Src/appserver.cpp \
    $$PWD/Src/binance.cpp \
    $$PWD/Src/binancekline.cpp \
    $$PWD/Src/bitget.cpp \
    $$PWD/Src/bitgetkline.cpp \
    $$PWD/Src/bingx.cpp \
    $$PWD/Src/bingxkline.cpp \
    $$PWD/Src/bybit.cpp \
    $$PWD/Src/bybitkline.cpp \
    $$PWD/Src/config.cpp \
    $$PWD/Src/core.cpp \
    $$PWD/Src/gate.cpp \
    $$PWD/Src/gatekline.cpp \
    $$PWD/Src/kucoin.cpp \
    $$PWD/Src/kucoinkline.cpp \
    $$PWD/Src/main.cpp \
    $$PWD/Src/mexc.cpp \
    $$PWD/Src/mexckline.cpp \
    $$PWD/Src/moex.cpp \
    $$PWD/Src/moexkline.cpp \
    $$PWD/Src/okx.cpp \
    $$PWD/Src/okxkline.cpp \
    $$PWD/Src/userscore.cpp \
    $$PWD/Src/usersdata.cpp

#inlude addition library
include($$PWD/../../Common/Common/Common.pri)
include($$PWD/../TradingCatCommon/TradingCatCommon.pri)
