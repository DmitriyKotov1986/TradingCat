#pragma once

//STL
#include <memory>

//QT
#include <QObject>
#include <QThread>

//My
#include <Common/tdbloger.h>
#include <Common/httpsslquery.h>

#include <TradingCatCommon/httpserver.h>
#include <TradingCatCommon/tradingdata.h>
#include <TradingCatCommon/detector.h>

#include <StockExchange/istockexchange.h>

#include "userscore.h"
#include "appserver.h"
#include "config.h"

class Core final
    : public QObject
{
    Q_OBJECT

public:
    explicit Core(QObject *parent = nullptr);
    ~Core();

    QString errorString();
    bool isError() const;

public slots:
    void start();      //запуск работы
    void stop();

signals:
    void stopAll();    //остановка
    void finished();

private slots:
    void errorOccurredLoger(Common::EXIT_CODE errorCode, const QString& errorString);

    void errorOccurredStockExchange(const TradingCatCommon::StockExchangeID& id, Common::EXIT_CODE errorCode, const QString& errorString);
    void sendLogMsgStockExchange(const TradingCatCommon::StockExchangeID& id, Common::TDBLoger::MSG_CODE category, const QString& msg);

    void errorOccurredTradingData(Common::EXIT_CODE errorCode, const QString& errorString);
    void sendLogMsgTradingData(Common::TDBLoger::MSG_CODE category, const QString& msg);
    void startedTradingData();

    void errorOccurredUsersCore(Common::EXIT_CODE errorCode, const QString& errorString);
    void sendLogMsgUsersCore(Common::TDBLoger::MSG_CODE category, const QString& msg);

    void errorOccurredDetector(Common::EXIT_CODE errorCode, const QString& errorString);
    void sendLogMsgDetector(Common::TDBLoger::MSG_CODE category, const QString& msg);

    void errorOccurredAppServer(Common::EXIT_CODE errorCode, const QString& errorString);
    void sendLogMsgAppServer(Common::TDBLoger::MSG_CODE category, const QString& msg);

private:
    std::unique_ptr<StockExchange::IStockExchange> makeStockEchange(const StockExchange::StockExchangeConfig& stockExchangeConfig) const;
    void makeProxyList();

private:
    Config *_cnf = nullptr;                            //Конфигурация
    Common::TDBLoger *_loger = nullptr;

    QString _errorString;

    Common::HTTPSSLQuery::ProxyList _proxyList;

    struct StockExchangeThread
    {
        std::unique_ptr<StockExchange::IStockExchange> stockExchange;
        std::unique_ptr<QThread> thread;
    };
    using PStockExchangeThread = std::unique_ptr<StockExchangeThread>;
    std::list<PStockExchangeThread> _stockExchangeThreadList;

    struct DataThread
    {
        std::unique_ptr<TradingCatCommon::TradingData> data;
        std::unique_ptr<QThread> thread;
    };
    std::unique_ptr<DataThread> _dataThread;

    struct AppServerThread
    {
        std::unique_ptr<AppServer> appServer;
        std::unique_ptr<QThread> thread;
    };
    std::unique_ptr<AppServerThread> _appServerThread;

    struct UsersCoreThread
    {
        std::unique_ptr<UsersCore> usersCore;
        std::unique_ptr<QThread> thread;
    };
    std::unique_ptr<UsersCoreThread> _usersCoreThread;

    struct DetectorThread
    {
        std::unique_ptr<TradingCatCommon::Detector> detector;
        std::unique_ptr<QThread> thread;
    };
    std::unique_ptr<DetectorThread> _detectorThread;

    bool _isStarted = false;

}; //class Core


