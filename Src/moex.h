#pragma once

//STL
#include <memory>
#include <unordered_map>

//Qt
#include <QObject>
#include <QByteArray>

//My
#include <Common/httpsslquery.h>

#include <TradingCatCommon/istockexchange.h>
#include <TradingCatCommon/klinehttppool.h>
#include <TradingCatCommon/tradingdata.h>
#include <TradingCatCommon/types.h>

class Moex final
    : public TradingCatCommon::IStockExchange
{
    Q_OBJECT

public:
    static const TradingCatCommon::StockExchangeID STOCK_ID;

public:
    Moex(const TradingCatCommon::StockExchangeConfig& config, const Common::HTTPSSLQuery::ProxyList& proxyList, QObject* parent = nullptr);
    ~Moex();

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
    Q_DISABLE_COPY_MOVE(Moex);

    void sendAuth();
    void sendUpdateSecurities(quint32 startLine);
    void restartUpdateSecurities();
    void parseSecurities(const QByteArray &answer);

    void makeKLines();

private:
    struct SecurityInfo
    {
        qint32 id = 0;
        QString secid;
        QString shortname;
        QString regnumber;
        QString name;
        QString isin;
        bool is_traded = false;
        qint32 emitent_id = 0;
        QString emitent_title;
        QString emitent_inn;
        QString emitent_okpo;
        QString gosreg;
        QString type;
        QString group;
        QString primary_boardid;
    };

    enum class RequestType: quint8
    {
        UNDEFINED = 0,
        UPDATE_SECURITIES = 1,
        AUTH = 2
    };

private:
    Common::HTTPSSLQuery::Headers _headers;
    Common::HTTPSSLQuery* _http = nullptr;

    const TradingCatCommon::StockExchangeConfig _config;
    const Common::HTTPSSLQuery::ProxyList _proxyList;

    TradingCatCommon::KLineHTTPPool* _pool = nullptr;

    std::unordered_map<quint64, RequestType> _requests;

    std::unordered_map<QString, std::unique_ptr<SecurityInfo>> _securities; ///< Список доступных инструментов

    quint32 _startLine = 0;

    bool _isStarted = false;
};

