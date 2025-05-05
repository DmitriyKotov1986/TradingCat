#pragma once

//STL
#include <memory>
#include <unordered_map>

//Qt
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QTimer>
#include <QSqlDatabase>
#include <QStringList>

//My
#include <Common/common.h>
#include <Common/tdbloger.h>

#include <TradingCatCommon/filter.h>
#include <TradingCatCommon/appserverprotocol.h>
#include <TradingCatCommon/tradingdata.h>
#include <TradingCatCommon/detector.h>

#include "usersdata.h"

class UsersCore
    : public QObject
{
    Q_OBJECT

public:
    explicit UsersCore(const Common::DBConnectionInfo& dbConnectionInfo, const TradingCatCommon::TradingData& tradingData, QObject *parent = nullptr);
    ~UsersCore() override;

    QString login(const TradingCatCommon::LoginQuery& query);
    QString logout(const TradingCatCommon::LogoutQuery& query);
    QString config(const TradingCatCommon::ConfigQuery& query);
    QString stockExchange(const TradingCatCommon::StockExchangesQuery& query);
    QString detect(const TradingCatCommon::DetectQuery& query);

    bool isOnline(int sessionId) const;

    QStringList usersOnline() const;

signals:
    /*!
        Сообщение логеру
        @param category - категория сообщения
        @param msg - текст сообщения
    */
    void sendLogMsg(Common::TDBLoger::MSG_CODE category, const QString& msg);

    /*!
        Сигнал генерируется если в процессе работы сервера произошла фатальная ошибка
        @param errorCode - код ошибки
        @param errorString - текстовое описание ошибки
    */
    void errorOccurred(Common::EXIT_CODE errorCode, const QString& errorString);

    void finished();

    void userOnline(qint64 sessionId, const TradingCatCommon::UserConfig& config);
    void userOffline(qint64 sessionId);

public slots:
    void start();
    void stop();

private slots:
    /*!
        Сообщение логеру
        @param category - категория сообщения
        @param msg - текст сообщения
    */
    void sendLogMsgUsers(Common::TDBLoger::MSG_CODE category, const QString& msg);

    /*!
        Сигнал генерируется если в процессе работы сервера произошла фатальная ошибка
        @param errorCode - код ошибки
        @param errorString - текстовое описание ошибки
    */
    void errorOccurredUsers(Common::EXIT_CODE errorCode, const QString& errorString);

    void connectionTimeout();

    void klineDetect(qint64 sessionId, const TradingCatCommon::Detector::PKLineDetectData& detectData);

private:
    UsersCore() = delete;
    Q_DISABLE_COPY_MOVE(UsersCore);

    static qint64 getId();

private:
    struct SessionData
    {
        QString user;
        QDateTime lastData = QDateTime::currentDateTime();        
        TradingCatCommon::Detector::KLinesDetectedList klinesDetectedList;
    };

private:
    const Common::DBConnectionInfo& _dbConnectionInfo;
    const TradingCatCommon::TradingData& _tradingData;

    Users* _users = nullptr; //данные пользователей

    std::unordered_map<qint64, SessionData> _onlineUsers;

    QTimer* _connetionTimeoutTimer = nullptr;

    bool _isStarted = false;
};
