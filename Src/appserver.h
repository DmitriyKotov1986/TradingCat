#pragma once

//STL
#include <memory>

//QT
#include <QObject>
#include <QHttpServer>
#include <QSqlDatabase>
#include <QJsonArray>
#include <QHash>
#include <QList>
#include <QTimer>
#include <QMutex>
#include <QDateTime>

//My
#include <Common/tdbloger.h>
#include <Common/common.h>

#include <TradingCatCommon/types.h>
#include <TradingCatCommon/stockexchange.h>
#include <TradingCatCommon/kline.h>
#include <TradingCatCommon/tradingdata.h>

#include "userscore.h"

class AppServer
    : public QObject
{
    Q_OBJECT

public:
    explicit AppServer(const TradingCatCommon::HTTPServerConfig& serverConfig,
                       const TradingCatCommon::TradingData& tradingData,
                       UsersCore& usersCore,
                       QObject* parent = nullptr);

    ~AppServer() override;

public slots:
    void start();
    void stop();

signals:
    /*!
        Сообщение логеру
        @param category - категория сообщения
        @param msg - текст сообщения
    */
    void sendLogMsg(Common::MSG_CODE category, const QString& msg);

    /*!
        Сигнал генерируется если в процессе работы сервера произошла фатальная ошибка
        @param errorCode - код ошибки
        @param errorString - текстовое описание ошибки
    */
    void errorOccurred(Common::EXIT_CODE errorCode, const QString& errorString);

    void finished();

private:
    AppServer() = delete;
    Q_DISABLE_COPY_MOVE(AppServer);

    bool makeServer();

    //answers
    QString loginUser(const QHttpServerRequest &request);
    QString logoutUser(const QHttpServerRequest &request);
    QString configUser(const QHttpServerRequest &request);
    QString detectData(const QHttpServerRequest &request);
    QString stockExchangesData(const QHttpServerRequest &request);
    QString klinesIdList(const QHttpServerRequest &request);
    QString serverStatus(const QHttpServerRequest &request);

private:
    const TradingCatCommon::HTTPServerConfig& _serverConfig;
    const TradingCatCommon::TradingData& _tradingData;
    UsersCore& _usersCore;

    std::unique_ptr<QHttpServer> _httpServer;
    std::unique_ptr<QTcpServer> _tcpServer;

    bool _isStarted = false;
    const QDateTime _startDateTime = QDateTime::currentDateTime();
};
