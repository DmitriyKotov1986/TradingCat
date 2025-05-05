#pragma once

//QT
#include <QString>
#include <QSet>

//My
#include <Common/sql.h>
#include <TradingCatCommon/types.h>
#include <TradingCatCommon/stockexchange.h>
#include <TradingCatCommon/httpserver.h>

class Config final
{
public:
    static Config* config(const QString& configFileName = QString());
    static void deleteConfig();

    static void makeConfig(const QString& configFileName);

private:
    explicit Config(const QString& configFileName);

public:
    //[DATABASE]
    const Common::DBConnectionInfo& dbConnectionInfo() const noexcept;

    //[SYSTEM]
    bool debugMode() const noexcept;
    const QString& logTableName() const noexcept;

    //SERVER
    const TradingCatCommon::HTTPServerConfig& httpServerConfig() const noexcept;

    //[PROXY_N]
    const TradingCatCommon::ProxyDataList& proxyDataList() const noexcept;

    [[nodiscard]] QString errorString();
    bool isError() const;

    //[STOCK_EXCHANGE_N]
    const TradingCatCommon::StockExchangeConfigList& stockExchangeConfigList() const noexcept;

private:
    const QString _configFileName;

    QString _errorString;

    //[SYSTEM]
    bool _debugMode = true;
    QString _logTableName;

    //[DATABASE]
    Common::DBConnectionInfo _dbConnectionInfo;

    //SERVER
    TradingCatCommon::HTTPServerConfig _httpServerConfig;

    //[PROXY_N]
    TradingCatCommon::ProxyDataList _proxyDataList;

    //[STOCK_EXCHANGE_N]
    TradingCatCommon::StockExchangeConfigList _stockExchangeConfigList;

};

