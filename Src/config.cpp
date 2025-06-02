//Qt
#include <QSettings>
#include <QFileInfo>
#include <QDebug>
#include <QDir>

//My
#include <Common/common.h>

#include "moex.h"
#include "mexc.h"
#include "gate.h"
#include "kucoin.h"
#include "bybit.h"
#include "bitmart.h"
#include "binance.h"
#include "bitget.h"
#include "bingx.h"
#include "okx.h"
#include "htx.h"
#include "kucoinfutures.h"
#include "bitgetfutures.h"
#include "gatefutures.h"
#include "bybitfutures.h"
#include "mexcfutures.h"

#include "config.h"

using namespace TradingCatCommon;
using namespace Common;

Q_GLOBAL_STATIC_WITH_ARGS(const QStringList, STOCK_NAME_LIST,
                          ({Moex::STOCK_ID.name,
                              Mexc::STOCK_ID.name,
                              Gate::STOCK_ID.name,
                              Kucoin::STOCK_ID.name,
                              Bybit::STOCK_ID.name,
                              Bitget::STOCK_ID.name,
                              Bitmart::STOCK_ID.name,
                              Bingx::STOCK_ID.name,
                              Okx::STOCK_ID.name,
                              Htx::STOCK_ID.name,
                              Binance::STOCK_ID.name,
                              KucoinFutures::STOCK_ID.name,
                              BitgetFutures::STOCK_ID.name,
                              BybitFutures::STOCK_ID.name,
                              GateFutures::STOCK_ID.name,
                              MexcFutures::STOCK_ID.name
                          }));

//static
static Config* config_ptr = nullptr;

Config* Config::config(const QString& configFileName)
{
    if (config_ptr == nullptr)
    {
        config_ptr = new Config(configFileName);
    }

    return config_ptr;
};

void Config::deleteConfig()
{
    Q_CHECK_PTR(config_ptr);

    if (config_ptr != nullptr)
    {
        delete config_ptr;

        config_ptr= nullptr;
    }
}

//class
Config::Config(const QString& configFileName) :
    _configFileName(configFileName)
{
    if (_configFileName.isEmpty())
    {
        _errorString = "Configuration file name cannot be empty";

        return;
    }
    if (!QFileInfo(_configFileName).exists())
    {
        _errorString = "Configuration file not exist. File name: " + _configFileName;

        return;
    }

    qInfo() << QString("%1 %2").arg(QTime::currentTime().toString(SIMPLY_TIME_FORMAT)).arg("Reading configuration from " +  _configFileName);

    QSettings ini(_configFileName, QSettings::IniFormat);

    QStringList groups = ini.childGroups();
    if (!groups.contains("DATABASE"))
    {
        _errorString = "Configuration file not contains [DATABASE] group";

        return;
    }
    if (!groups.contains("SERVER"))
    {
        _errorString = "Configuration file not contains [SERVER] group";

        return;
    }

    //Database
    ini.beginGroup("DATABASE");

    _dbConnectionInfo.driver = ini.value("Driver", "").toString();
    _dbConnectionInfo.dbName = ini.value("DataBase", "").toString();
    _dbConnectionInfo.userName = ini.value("UID", "").toString();
    _dbConnectionInfo.password = ini.value("PWD", "").toString();
    _dbConnectionInfo.connectOptions = ini.value("ConnectionOptions", "").toString();
    _dbConnectionInfo.port = ini.value("Port", "").toUInt();
    _dbConnectionInfo.host = ini.value("Host", "").toString();

    const auto errDataBase = _dbConnectionInfo.check();

    if (!errDataBase.isEmpty())
    {
        _errorString = QString("Incorrect value [DATABASE]. Error: %1").arg(errDataBase);

        return;
    }

    ini.endGroup();

    //SYSTEM
    ini.beginGroup("SYSTEM");

    _debugMode = ini.value("DebugMode", "0").toBool();
    _logTableName = ini.value("LogTableName", "").toString();

    ini.endGroup();

    //SERVER
    ini.beginGroup("SERVER");

    const auto address_str =ini.value("Address", QHostAddress::LocalHost).toString();
    _httpServerConfig.address = QHostAddress(address_str);
    if (_httpServerConfig.address.isNull())
    {
        _errorString = QString("Invalid value in [SERVER]/Address. Value: %1").arg(address_str);

        return;
    }
    _httpServerConfig.port = ini.value("Port", 80).toUInt();
    if (_httpServerConfig.port == 0)
    {
        _errorString = QString("Value in [SERVER]/Port must be number");

        return;
    }
    _httpServerConfig.maxUsers = ini.value("MaxUsers", 100).toUInt();
    if (_httpServerConfig.maxUsers == 0)
    {
        _errorString = QString("Value in [SERVER]/MaxUsers must be number");

        return;
    }
    _httpServerConfig.rootDir = ini.value("RootDir", QCoreApplication::applicationDirPath()).toString();
    _httpServerConfig.name = ini.value("Name", "").toString();

    ini.endGroup();

    //PROXY_N
    for (quint8 currentProxyIndex = 0; currentProxyIndex < std::numeric_limits<quint8>().max(); ++currentProxyIndex)
    {
        const auto group =  QString("PROXY_%1").arg(currentProxyIndex);
        if (groups.contains(group))
        {
            ini.beginGroup(group);

            TradingCatCommon::ProxyData tmp;
            tmp.address = QHostAddress(ini.value("Host", "").toString());
            if (tmp.address.isNull())
            {
                _errorString = QString("Value in [%1]/Host is not valid").arg(group);

                return;
            }

            bool ok = false;
            tmp.port = ini.value("Port", 51888).toUInt(&ok);
            if (tmp.port == 0)
            {
                _errorString = QString("Value in [%1]/Port must be number").arg(group);

                return;
            }

            tmp.user = ini.value("User").toString();
            tmp.password = ini.value("Password").toString();

            ini.endGroup();

            _proxyDataList.emplace_back(std::move(tmp));
        }
    }

    //STOCK_EXCHANGE_N
    for (quint8 currentStockExchangeIndex = 0; currentStockExchangeIndex < std::numeric_limits<quint8>().max(); ++currentStockExchangeIndex)
    {
        const auto group = QString("STOCK_EXCHANGE_%1").arg(currentStockExchangeIndex);
        if (groups.contains(group))
        {
            ini.beginGroup(group);

            StockExchangeConfig tmp;

            tmp.type = ini.value("Type", "").toString();
            if (tmp.type.isEmpty() || !STOCK_NAME_LIST->contains(tmp.type))
            {
                _errorString = QString("Value in [%1]/Type cannot by empty or undefined or incorrect. Support: %2").arg(group).arg(STOCK_NAME_LIST->join(','));

                return;
            }

            tmp.user = ini.value("User", "").toString();
            tmp.password = ini.value("Password", "").toString();

            tmp.klineTypes = stringToKLineTypes(ini.value("KLineTypes", "1m").toString());
            if (tmp.klineTypes.empty())
            {
                _errorString = QString("Value in [%1]/KLineTypes cannot by empty or undefined or incorrect").arg(group);

                return;
            }

            tmp.klineNames = ini.value("KLineNames").toString().split(',');
            if (tmp.klineNames.first().isEmpty())
            {
                tmp.klineNames.clear();
            }

            ini.endGroup();

            _stockExchangeConfigList.emplace_back(std::move(tmp));
        }
    }

    if (_stockExchangeConfigList.empty())
    {
        _errorString = QString("There are no stock exchanges in the configuration. Check [STOCK_EXCHANGE_0] group");

        return;
    }
}

const DBConnectionInfo &Config::dbConnectionInfo() const noexcept
{
    return _dbConnectionInfo;
}

bool Config::debugMode() const noexcept
{
    return _debugMode;
}

const QString& Config::logTableName() const noexcept
{
    return _logTableName;
}

const HTTPServerConfig &Config::httpServerConfig() const noexcept
{
    return _httpServerConfig;
}

void Config::makeConfig(const QString& configFileName)
{
    if (configFileName.isEmpty())
    {
        qWarning() << "Configuration file name cannot be empty";

        return;
    }

    QSettings ini(configFileName, QSettings::IniFormat);

    if (!ini.isWritable())
    {
        qWarning() << QString("Can not write configuration file: %1").arg(configFileName);

        return;
    }

    ini.clear();

    //Database
    ini.beginGroup("DATABASE");

    ini.remove("");

    ini.setValue("Driver", "QMYSQL");
    ini.setValue("DataBase", "TradingCat");
    ini.setValue("UID", "user");
    ini.setValue("PWD", "password");
    ini.setValue("ConnectionOptions", "");
    ini.setValue("Port", "3306");
    ini.setValue("Host", "localhost");

    ini.endGroup();

    //System
    ini.beginGroup("SYSTEM");

    ini.remove("");

    ini.setValue("DebugMode", true);
    ini.setValue("LogTableName", QString("%1Log").arg(QCoreApplication::applicationName()));

    ini.endGroup();

    //SERVER
    ini.beginGroup("SERVER");

    ini.remove("");

    ini.setValue("Address", QHostAddress::LocalHost);
    ini.setValue("Port", 80);
    ini.setValue("RootDir", QCoreApplication::applicationDirPath());
    ini.setValue("CRTFileName", "");
    ini.setValue("KEYFileName", "");
    ini.setValue("Name", "MyServer");

    ini.endGroup();

    //PROXY
    ini.beginGroup("PROXY_0");

    ini.remove("");

    ini.setValue("Host", "localhost");
    ini.setValue("Port", "1234");
    ini.setValue("User", "user");
    ini.setValue("Password", "password");

    ini.endGroup();

    //PROXY
    ini.beginGroup("STOCK_EXCHANGE_0");

    ini.remove("");

    ini.setValue("Type", STOCK_NAME_LIST->join(','));
    ini.setValue("User", "user");
    ini.setValue("Password", "password");
    ini.setValue("KLineTypes", "1m,5m,10m,1h,1w,1d");
    ini.setValue("KLineNames", "");

    ini.endGroup();

    //сбрасываем буфер
    ini.sync();

    qInfo() << QString("Save configuration to %1").arg(configFileName);
}

const TradingCatCommon::ProxyDataList& Config::proxyDataList() const noexcept
{
    return _proxyDataList;
}

QString Config::errorString()
{
    auto res = _errorString;
    _errorString.clear();

    return res;
}


bool Config::isError() const
{
    return !_errorString.isEmpty();
}

const StockExchangeConfigList &Config::stockExchangeConfigList() const noexcept
{
    return _stockExchangeConfigList;
}
