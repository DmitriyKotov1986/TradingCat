#pragma once

//Qt
#include <QObject>
#include <QByteArray>

//My
#include <Common/httpsslquery.h>

#include <TradingCatCommon/istockexchange.h>
#include <TradingCatCommon/klinehttppool.h>
#include <TradingCatCommon/tradingdata.h>
#include <TradingCatCommon/types.h>

class GateFutures final
    : public TradingCatCommon::IStockExchange
{
    Q_OBJECT

public:
    static const TradingCatCommon::StockExchangeID STOCK_ID;

public:
    GateFutures(const TradingCatCommon::StockExchangeConfig& config, const Common::HTTPSSLQuery::ProxyList& proxyList, QObject* parent = nullptr);
    ~GateFutures();

    void start() override;
    void stop() override;

private slots:
    void getAnswerHTTP(const QByteArray& answer, quint64 id);
    void errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString& msg, quint64 id);
    void sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString& msg, quint64 id);

    void getKLinesPool(const TradingCatCommon::PKLinesList& klines);
    void errorOccurredPool(Common::EXIT_CODE errorCode, const QString& errorString);
    void sendLogMsgPool(Common::TDBLoger::MSG_CODE category, const QString& msg);

private:
    Q_DISABLE_COPY_MOVE(GateFutures);

    void sendUpdateMoney();
    void restartUpdateMoney();
    void parseMoney(const QByteArray &answer);

    void makeKLines();

private:
    Common::HTTPSSLQuery::Headers _headers;
    Common::HTTPSSLQuery* _http = nullptr;
    const TradingCatCommon::StockExchangeConfig _config;
    const Common::HTTPSSLQuery::ProxyList _proxyList;

    TradingCatCommon::KLineHTTPPool* _pool = nullptr;

    quint64 _currentRequestId = 0;

    std::list<TradingCatCommon::KLineID> _money; ///< Список доступных инструментов
    std::list<QString> _symbols; ///< Список доступных инструментов

    bool _isStarted = false;
};

