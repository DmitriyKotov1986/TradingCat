//QT
#include <QtNetwork/QNetworkProxy>
#include <QCoreApplication>

//My
#include "moex.h"
#include "mexc.h"
#include "gate.h"
#include "kucoin.h"
#include "bybit.h"
#include "binance.h"
#include "bitget.h"
#include "bingx.h"
#include "okx.h"
#include "kucoinfutures.h"
#include "bitgetfutures.h"

#include "core.h"

using namespace TradingCatCommon;
using namespace Common;

Core::Core(QObject *parent)
    : QObject{parent}
    , _cnf(Config::config())
    , _loger(TDBLoger::DBLoger())
{
    Q_CHECK_PTR(_cnf);
    Q_CHECK_PTR(_loger);

    QObject::connect(_loger, SIGNAL(errorOccurred(Common::EXIT_CODE, const QString&)),
                     SLOT(errorOccurredLoger(Common::EXIT_CODE, const QString&)), Qt::DirectConnection);

    makeProxyList();
}

Core::~Core()
{
    stop();
}

void Core::start()
{
    Q_CHECK_PTR(_loger);
    Q_CHECK_PTR(_cnf);


    //Data thread
    {
        TradingCatCommon::StockExchangesIDList stockExchangeIdList;
        for (const auto& stockExchangeIdConfig: _cnf->stockExchangeConfigList())
        {
            stockExchangeIdList.emplace(StockExchangeID(stockExchangeIdConfig.type));
        }

        _dataThread = std::make_unique<DataThread>();
        _dataThread->data = std::make_unique<TradingData>(stockExchangeIdList);
        _dataThread->thread = std::make_unique<QThread>();
        _dataThread->data->moveToThread(_dataThread->thread.get());

        connect(_dataThread->thread.get(), SIGNAL(started()), _dataThread->data.get(), SLOT(start()), Qt::DirectConnection);
        connect(_dataThread->data.get(), SIGNAL(finished()), _dataThread->thread.get(), SLOT(quit()), Qt::DirectConnection);
        connect(this, SIGNAL(stopAll()), _dataThread->data.get(), SLOT(stop()), Qt::QueuedConnection);
    }

    //UsersCore
    {
        _usersCoreThread = std::make_unique<UsersCoreThread>();
        _usersCoreThread->usersCore = std::make_unique<UsersCore>(_cnf->dbConnectionInfo(), *_dataThread->data);
        _usersCoreThread->thread = std::make_unique<QThread>();
        _usersCoreThread->usersCore->moveToThread(_usersCoreThread->thread.get());

        connect(_usersCoreThread->thread.get(), SIGNAL(started()), _usersCoreThread->usersCore.get(), SLOT(start()), Qt::DirectConnection);
        connect(_usersCoreThread->usersCore.get(), SIGNAL(finished()), _usersCoreThread->thread.get(), SLOT(quit()), Qt::DirectConnection);
        connect(this, SIGNAL(stopAll()), _usersCoreThread->usersCore.get(), SLOT(stop()), Qt::QueuedConnection);
        connect(_dataThread->thread.get(), SIGNAL(started()), _usersCoreThread->thread.get(), SLOT(start()), Qt::QueuedConnection); //start afret DataThread

        connect(_usersCoreThread->usersCore.get(), SIGNAL(errorOccurred(Common::EXIT_CODE, const QString&)),
                SLOT(errorOccurredUsersCore(Common::EXIT_CODE, const QString&)), Qt::QueuedConnection);
        connect(_usersCoreThread->usersCore.get(), SIGNAL(sendLogMsg(Common::TDBLoger::MSG_CODE, const QString&)),
                SLOT(sendLogMsgUsersCore(Common::TDBLoger::MSG_CODE, const QString&)), Qt::QueuedConnection);

    }

    // Detector
    {
        _detectorThread = std::make_unique<DetectorThread>();
        _detectorThread->detector = std::make_unique<Detector>(*_dataThread->data);
        _detectorThread->thread = std::make_unique<QThread>();
        _detectorThread->detector->moveToThread(_detectorThread->thread.get());

        connect(_detectorThread->thread.get(), SIGNAL(started()), _detectorThread->detector.get(), SLOT(start()), Qt::DirectConnection);
        connect(_detectorThread->detector.get(), SIGNAL(finished()), _detectorThread->thread.get(), SLOT(quit()), Qt::DirectConnection);
        connect(this, SIGNAL(stopAll()), _detectorThread->detector.get(), SLOT(stop()), Qt::QueuedConnection);
        connect(_usersCoreThread->thread.get(), SIGNAL(started()), _detectorThread->thread.get(), SLOT(start()), Qt::QueuedConnection); //start afret usersCoreThread

        connect(_detectorThread->detector.get(), SIGNAL(errorOccurred(Common::EXIT_CODE, const QString&)),
                SLOT(errorOccurredDetector(Common::EXIT_CODE, const QString&)), Qt::QueuedConnection);
        connect(_detectorThread->detector.get(), SIGNAL(sendLogMsg(Common::TDBLoger::MSG_CODE, const QString&)),
                SLOT(sendLogMsgDetector(Common::TDBLoger::MSG_CODE, const QString&)), Qt::QueuedConnection);

        connect(_usersCoreThread->usersCore.get(), SIGNAL(userOnline(qint64, const TradingCatCommon::UserConfig&)),
                _detectorThread->detector.get(), SLOT(userOnline(qint64, const TradingCatCommon::UserConfig&)));
        connect(_usersCoreThread->usersCore.get(), SIGNAL(userOffline(qint64)),
                _detectorThread->detector.get(), SLOT(userOffline(qint64)));

        connect(_detectorThread->detector.get(), SIGNAL(klineDetect(qint64, const TradingCatCommon::Detector::PKLineDetectData&)),
                _usersCoreThread->usersCore.get(), SLOT(klineDetect(qint64, const TradingCatCommon::Detector::PKLineDetectData&)));


    }

    //Stock exchange
    {
        for (const auto& stockExchangeConfig: _cnf->stockExchangeConfigList())
        {
            auto tmp = std::make_unique<StockExchangeThread>();
            tmp->stockExchange = makeStockEchange(stockExchangeConfig);
            if (!tmp->stockExchange)
            {
                const auto msg = QString("Critical error while the Core running. Code: %1 Message: Undefined stock exchange type").arg(Common::EXIT_CODE::LOAD_CONFIG_ERR);

                qCritical() << msg;
                QCoreApplication::exit(Common::EXIT_CODE::LOAD_CONFIG_ERR);

                return;
            }

            tmp->thread = std::make_unique<QThread>();
            tmp->stockExchange->moveToThread(tmp->thread.get());

            connect(tmp->thread.get(), SIGNAL(started()), tmp->stockExchange.get(), SLOT(start()), Qt::DirectConnection);
            connect(tmp->stockExchange.get(), SIGNAL(finished()), tmp->thread.get(), SLOT(quit()), Qt::DirectConnection);
            connect(this, SIGNAL(stopAll()), tmp->stockExchange.get(), SLOT(stop()), Qt::QueuedConnection);
            connect(_usersCoreThread->thread.get(), SIGNAL(started()), tmp->thread.get(), SLOT(start()), Qt::QueuedConnection); //start afret UsersCoreThread

            connect(tmp->stockExchange.get(), SIGNAL(errorOccurred(const TradingCatCommon::StockExchangeID&, Common::EXIT_CODE, const QString&)),
                    SLOT(errorOccurredStockExchange(const TradingCatCommon::StockExchangeID&, Common::EXIT_CODE, const QString&)), Qt::QueuedConnection);
            connect(tmp->stockExchange.get(), SIGNAL(sendLogMsg(const TradingCatCommon::StockExchangeID&, Common::TDBLoger::MSG_CODE, const QString&)),
                    SLOT(sendLogMsgStockExchange(const TradingCatCommon::StockExchangeID&, Common::TDBLoger::MSG_CODE, const QString&)), Qt::QueuedConnection);

            // get new data
            connect(tmp->stockExchange.get(), SIGNAL(getKLines(const TradingCatCommon::StockExchangeID&, const TradingCatCommon::PKLinesList&)),
                    _dataThread->data.get(), SLOT(addKLines(const TradingCatCommon::StockExchangeID&, const TradingCatCommon::PKLinesList&)), Qt::QueuedConnection);
            connect(tmp->stockExchange.get(), SIGNAL(getKLines(const TradingCatCommon::StockExchangeID&, const TradingCatCommon::PKLinesList&)),
                    _detectorThread->detector.get(), SLOT(addKLines(const TradingCatCommon::StockExchangeID&, const TradingCatCommon::PKLinesList&)), Qt::QueuedConnection);

            _stockExchangeThreadList.emplace_back(std::move(tmp));
        }
    }

    // App Server
    {
        _appServerThread = std::make_unique<AppServerThread>();
        _appServerThread->appServer = std::make_unique<AppServer>(_cnf->httpServerConfig(), *_dataThread->data, *_usersCoreThread->usersCore);

        _appServerThread->thread = std::make_unique<QThread>();
        _appServerThread->appServer->moveToThread(_appServerThread->thread.get());

        connect(_appServerThread->thread.get(), SIGNAL(started()), _appServerThread->appServer.get(), SLOT(start()), Qt::DirectConnection);
        connect(_appServerThread->appServer.get(), SIGNAL(finished()), _appServerThread->thread.get(), SLOT(quit()), Qt::DirectConnection);
        connect(this, SIGNAL(stopAll()), _appServerThread->appServer.get(), SLOT(stop()), Qt::QueuedConnection);
        connect(_usersCoreThread->thread.get(), SIGNAL(started()), _appServerThread->thread.get(), SLOT(start()), Qt::QueuedConnection); //start afret UsersCoreThread

        connect(_appServerThread->appServer.get(), SIGNAL(errorOccurred(Common::EXIT_CODE, const QString&)),
                SLOT(errorOccurredAppServer(Common::EXIT_CODE, const QString&)), Qt::QueuedConnection);
        connect(_appServerThread->appServer.get(), SIGNAL(sendLogMsg(Common::TDBLoger::MSG_CODE, const QString&)),
                SLOT(sendLogMsgAppServer(Common::TDBLoger::MSG_CODE, const QString&)), Qt::QueuedConnection);
    }

    _isStarted = true;

    _dataThread->thread->start();

    _loger->sendLogMsg(TDBLoger::MSG_CODE::INFORMATION_CODE, "Started successfully");

//    QTimer::singleShot(60000, this, [this](){ this->stop(); } );
}

void Core::stop()
{
    if (!_isStarted)
    {
        return;
    }

    Q_CHECK_PTR(_loger);

    emit stopAll();

    for (const auto& stockExchangeThread: _stockExchangeThreadList)
    {
        stockExchangeThread->thread->wait();
    }
    _stockExchangeThreadList.clear();

    _appServerThread->thread->wait();
    _appServerThread.reset();

    _usersCoreThread->thread->wait();
    _usersCoreThread.reset();

    _dataThread->thread->wait();
    _dataThread.reset();

    _isStarted = false;

    _loger->sendLogMsg(TDBLoger::MSG_CODE::INFORMATION_CODE, "Stoped successfully");

    emit finished();
}

void Core::errorOccurredLoger(Common::EXIT_CODE errorCode, const QString &errorString)
{
    const auto msg = QString("Critical error while the Loger is running. Code: %1 Message: %2").arg(errorCode).arg(errorString);

    qCritical() << msg;

    QCoreApplication::exit(errorCode);
}

void Core::errorOccurredStockExchange(const TradingCatCommon::StockExchangeID &id, Common::EXIT_CODE errorCode, const QString &errorString)
{
    const auto msg = QString("Critical error while the Stock Exchange %1 is running. Code: %2 Message: %3").arg(id.toString()).arg(errorCode).arg(errorString);

    _loger->sendLogMsg(TDBLoger::MSG_CODE::CRITICAL_CODE, msg);

    qCritical() << msg;

    QCoreApplication::exit(errorCode);
}

void Core::sendLogMsgStockExchange(const TradingCatCommon::StockExchangeID &id, Common::TDBLoger::MSG_CODE category, const QString &msg)
{
    _loger->sendLogMsg(category, QString("Stock exchange %1: %2").arg(id.toString()).arg(msg));
}

void Core::errorOccurredTradingData(Common::EXIT_CODE errorCode, const QString &errorString)
{
    const auto msg = QString("Critical error while the TradingData is running. Code: %1 Message: %2").arg(errorCode).arg(errorString);

    _loger->sendLogMsg(TDBLoger::MSG_CODE::CRITICAL_CODE, msg);

    qCritical() << msg;

    QCoreApplication::exit(errorCode);
}

void Core::sendLogMsgTradingData(Common::TDBLoger::MSG_CODE category, const QString &msg)
{
    _loger->sendLogMsg(category, QString("TradingData: %1").arg(msg));
}

void Core::errorOccurredUsersCore(Common::EXIT_CODE errorCode, const QString &errorString)
{
    const auto msg = QString("Critical error while the UsersCore is running. Code: %1 Message: %2").arg(errorCode).arg(errorString);

    _loger->sendLogMsg(TDBLoger::MSG_CODE::CRITICAL_CODE, msg);

    qCritical() << msg;

    QCoreApplication::exit(errorCode);
}

void Core::sendLogMsgUsersCore(Common::TDBLoger::MSG_CODE category, const QString &msg)
{
    _loger->sendLogMsg(category, QString("Users core: %1").arg(msg));
}

void Core::errorOccurredDetector(Common::EXIT_CODE errorCode, const QString &errorString)
{
    const auto msg = QString("Critical error while the Detector is running. Code: %1 Message: %2").arg(errorCode).arg(errorString);

    _loger->sendLogMsg(TDBLoger::MSG_CODE::CRITICAL_CODE, msg);

    qCritical() << msg;

    QCoreApplication::exit(errorCode);
}

void Core::sendLogMsgDetector(Common::TDBLoger::MSG_CODE category, const QString &msg)
{
    _loger->sendLogMsg(category, QString("Detector: %1").arg(msg));
}

void Core::errorOccurredAppServer(Common::EXIT_CODE errorCode, const QString &errorString)
{
    const auto msg = QString("Critical error while the HTTP server is running. Code: %1 Message: %2").arg(errorCode).arg(errorString);

    _loger->sendLogMsg(TDBLoger::MSG_CODE::CRITICAL_CODE, msg);

    qCritical() << msg;

    QCoreApplication::exit(errorCode);
}

void Core::sendLogMsgAppServer(Common::TDBLoger::MSG_CODE category, const QString &msg)
{
    _loger->sendLogMsg(category, QString("Application HTTP server: %1").arg(msg));
}

std::unique_ptr<IStockExchange> Core::makeStockEchange(const TradingCatCommon::StockExchangeConfig& stockExchangeConfig) const
{
    // Spot
    if (stockExchangeConfig.type == Moex::STOCK_ID)
    {
        return std::make_unique<Moex>(stockExchangeConfig, _proxyList);
    }
    else if (stockExchangeConfig.type == Mexc::STOCK_ID)
    {
        return std::make_unique<Mexc>(stockExchangeConfig, _proxyList);
    }
    else if (stockExchangeConfig.type == Gate::STOCK_ID)
    {
        return std::make_unique<Gate>(stockExchangeConfig, _proxyList);
    }
    else if (stockExchangeConfig.type == Kucoin::STOCK_ID)
    {
        return std::make_unique<Kucoin>(stockExchangeConfig, _proxyList);
    }
    else if (stockExchangeConfig.type == Bybit::STOCK_ID)
    {
        return std::make_unique<Bybit>(stockExchangeConfig, _proxyList);
    }
    else if (stockExchangeConfig.type == Binance::STOCK_ID)
    {
        return std::make_unique<Binance>(stockExchangeConfig, _proxyList);
    }
    else if (stockExchangeConfig.type == Bitget::STOCK_ID)
    {
        return std::make_unique<Bitget>(stockExchangeConfig, _proxyList);
    }
    else if (stockExchangeConfig.type == Bingx::STOCK_ID)
    {
        return std::make_unique<Bingx>(stockExchangeConfig, _proxyList);
    }
    else if (stockExchangeConfig.type == Okx::STOCK_ID)
    {
        return std::make_unique<Okx>(stockExchangeConfig, _proxyList);
    }

    // Futures
    else if (stockExchangeConfig.type == KucoinFutures::STOCK_ID)
    {
        return std::make_unique<KucoinFutures>(stockExchangeConfig, _proxyList);
    }
    else if (stockExchangeConfig.type == BitgetFutures::STOCK_ID)
    {
        return std::make_unique<BitgetFutures>(stockExchangeConfig, _proxyList);
    }

    return nullptr;
}

void Core::makeProxyList()
{
    _proxyList.clear();

    for (const auto& proxyInfo: _cnf->proxyDataList())
    {
        QNetworkProxy proxy;
        proxy.setType(QNetworkProxy::HttpProxy);
        proxy.setHostName(proxyInfo.address.toString());
        proxy.setPort(proxyInfo.port);
        proxy.setUser(proxyInfo.user);
        proxy.setPassword(proxyInfo.password);

        _proxyList.emplace_back(std::move(proxy));
    }
}

QString Core::errorString()
{
    auto res = _errorString;
    _errorString.clear();

    return res;
}

bool Core::isError() const
{
    return !_errorString.isEmpty();
}

