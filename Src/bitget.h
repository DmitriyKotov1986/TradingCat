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

class Bitget final
    : public TradingCatCommon::IStockExchange
{
    Q_OBJECT

public:
    static const TradingCatCommon::StockExchangeID STOCK_ID;

public:
    Bitget(const TradingCatCommon::StockExchangeConfig& config, const Common::HTTPSSLQuery::ProxyList& proxyList, QObject* parent = nullptr);
    ~Bitget();

    void start() override;
    void stop() override;

private slots:
    void getAnswerHTTP(const QByteArray& answer, quint64 id);
    void errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString& msg, quint64 id, const QByteArray& answer);
    void sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString& msg, quint64 id);

    void getKLinesPool(const TradingCatCommon::PKLinesList& klines);
    void errorOccurredPool(Common::EXIT_CODE errorCode, const QString& errorString);
    void sendLogMsgPool(Common::TDBLoger::MSG_CODE category, const QString& msg);

private:
    Q_DISABLE_COPY_MOVE(Bitget);

    void sendUpdateMoney();
    void restartUpdateMoney();
    void parseMoney(const QByteArray &answer);

    void makeKLines(const TradingCatCommon::PKLinesIDList klinesIdList);

private:
    Common::HTTPSSLQuery::Headers _headers;
    Common::HTTPSSLQuery* _http = nullptr;
    const TradingCatCommon::StockExchangeConfig _config;
    const Common::HTTPSSLQuery::ProxyList _proxyList;

    TradingCatCommon::KLineHTTPPool* _pool = nullptr;

    quint64 _currentRequestId = 0;

    bool _isStarted = false;
};

