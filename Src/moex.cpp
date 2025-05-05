//Qt
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

//My
#include <Common/parser.h>
#include <TradingCatCommon/detector.h>

#include "moexkline.h"

#include "moex.h"

using namespace Common;
using namespace TradingCatCommon;

//static
Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://iss.moex.com/"));

const StockExchangeID Moex::STOCK_ID("MOEX");

///////////////////////////////////////////////////////////////////////////////
/// class Moex
///
Moex::Moex(const TradingCatCommon::StockExchangeConfig& config, const Common::HTTPSSLQuery::ProxyList& proxyList, QObject *parent)
    : IStockExchange{STOCK_ID, parent}
    , _config(config)
    , _proxyList(proxyList)
{
    _headers.insert("Content-Type", "application/json; charset=utf-8");
}

Moex::~Moex()
{
    stop();
}

void Moex::start()
{
    Q_ASSERT(!_isStarted);

    _http = new Common::HTTPSSLQuery(_proxyList);
    _http->setHeaders(_headers);

    QObject::connect(_http, SIGNAL(getAnswer(const QByteArray&, quint64)),
                     SLOT(getAnswerHTTP(const QByteArray&, quint64)));
    QObject::connect(_http, SIGNAL(errorOccurred(QNetworkReply::NetworkError, quint64, const QString&, quint64)),
                     SLOT(errorOccurredHTTP(QNetworkReply::NetworkError, quint64, const QString&, quint64)));
    QObject::connect(_http, SIGNAL(sendLogMsg(Common::TDBLoger::MSG_CODE, const QString&, quint64)),
                     SLOT(sendLogMsgHTTP(Common::TDBLoger::MSG_CODE, const QString&, quint64)));

    if (!_pool)
    {
        _pool = new KLineHTTPPool(_http);

        QObject::connect(_pool, SIGNAL(getKLines(const TradingCatCommon::PKLinesList&)),
                         SLOT(getKLinesPool(const TradingCatCommon::PKLinesList&)));
        QObject::connect(_pool, SIGNAL(errorOccurred(Common::EXIT_CODE, const QString&)),
                         SLOT(errorOccurredPool(Common::EXIT_CODE, const QString&)));
        QObject::connect(_pool, SIGNAL(sendLogMsg(Common::TDBLoger::MSG_CODE, const QString&)),
                         SLOT(sendLogMsgPool(Common::TDBLoger::MSG_CODE, const QString&)));


        if (!_config.user.isEmpty())
        {
            _pool->setUserPassword(_config.user, _config.password);
        }
    }

    _isStarted = true;

    if (_config.user.isEmpty())
    {
        sendUpdateSecurities(_startLine);
    }
    else
    {
        sendAuth();
    }
}

void Moex::stop()
{
    if (!_isStarted)
    {
        emit finished();

        return;
    }

    delete _http;
    delete _pool;

    _isStarted = false;

    emit finished();
}

void Moex::getAnswerHTTP(const QByteArray &answer, quint64 id)
{
    const auto it_request = _requests.find(id);

    if (it_request == _requests.end())
    {
        return;
    }

    switch (it_request->second)
    {
    case RequestType::AUTH:
        emit sendLogMsg(STOCK_ID, TDBLoger::MSG_CODE::INFORMATION_CODE, "Athentication on stock exchange was successfully");
        _headers.emplace("Authorization", QString("Bearer %1").arg(answer).toUtf8());
        _headers.emplace("Cookie", QString("MicexPassportCert=%1").arg(answer).toUtf8());
        _http->setHeaders(_headers);
        sendUpdateSecurities(_startLine);
        break;
    case RequestType::UPDATE_SECURITIES:
        parseSecurities(answer);
        break;
    default:
        Q_ASSERT(false);
    }

    _requests.erase(id);
}

void Moex::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id)
{
    Q_UNUSED(code);
    Q_UNUSED(serverCode);

    emit sendLogMsg(STOCK_ID, Common::TDBLoger::MSG_CODE::WARNING_CODE, QString("HTTP request %1 failed with an error: %2").arg(id).arg(msg));

    const auto it_request = _requests.find(id);

    if (it_request == _requests.end())
    {
        return;
    }

    switch (it_request->second)
    {
    case RequestType::AUTH:
        emit sendLogMsg(STOCK_ID, TDBLoger::MSG_CODE::WARNING_CODE, "Cannot authentication on stock exchange. Contionue without authentication...");
        sendUpdateSecurities(_startLine);
        break;
    case RequestType::UPDATE_SECURITIES:
        restartUpdateSecurities();
        break;
    default:
        Q_ASSERT(false);
    }

    _requests.erase(id);
}

void Moex::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    emit sendLogMsg(STOCK_ID, category, QString("HTTP request %1: %2").arg(id).arg(msg));
}

void Moex::getKLinesPool(const TradingCatCommon::PKLinesList &klines)
{
    Q_ASSERT(!klines->empty());

#ifdef QT_DEBUG
    const auto currDateTime = QDateTime::currentDateTime();
    QDateTime start = currDateTime.addYears(100);
    QDateTime end = currDateTime.addYears(-100);
    for (const auto& kline: *klines)
    {
        start = std::min(start, kline->closeTime);
        end = std::max(end, kline->closeTime);
    }


    emit sendLogMsg(STOCK_ID, Common::TDBLoger::MSG_CODE::INFORMATION_CODE, QString("Get new klines: %1. Count: %2 from %3 to %4")
                                                                                .arg(klines->begin()->get()->id.toString())
                                                                                .arg(klines->size())
                                                                                .arg(start.toString(SIMPLY_DATETIME_FORMAT))
                                                                                .arg(end.toString(SIMPLY_DATETIME_FORMAT)));
#endif

    emit getKLines(STOCK_ID, klines);
}

void Moex::errorOccurredPool(Common::EXIT_CODE errorCode, const QString &errorString)
{
    emit errorOccurred(STOCK_ID, errorCode, QString("KLines Pool error: %1").arg(errorString));
}

void Moex::sendLogMsgPool(Common::TDBLoger::MSG_CODE category, const QString &msg)
{
    emit sendLogMsg(STOCK_ID, category, QString("KLines Pool: %1").arg(msg));
}

void Moex::sendAuth()
{
    QUrl url("https://passport.moex.com/authenticate");

    const auto id = _http->send(url, Common::HTTPSSLQuery::RequestType::GET);

    _requests.emplace(id, RequestType::AUTH);
}

void Moex::sendUpdateSecurities(quint32 startLine)
{
    Q_CHECK_PTR(_http);

    Q_ASSERT(_isStarted);

    if (startLine == 0)
    {
        emit sendLogMsg(STOCK_ID, TDBLoger::MSG_CODE::INFORMATION_CODE, QString("Start search for the list of securities"));
    }

    QUrlQuery urlQuery;
    urlQuery.addQueryItem("iss.meta", "off");
    urlQuery.addQueryItem("limit", "100");
    urlQuery.addQueryItem("start", QString::number(startLine));
    urlQuery.addQueryItem("engine", "stock");
    urlQuery.addQueryItem("is_trading", "1");
    urlQuery.addQueryItem("market", "shares");

    QUrl url(*BASE_URL);
    url.setPath("/iss/securities.json");
    url.setQuery(urlQuery);

    const auto id = _http->send(url, Common::HTTPSSLQuery::RequestType::GET);

    _requests.emplace(id, RequestType::UPDATE_SECURITIES);
}

void Moex::restartUpdateSecurities()
{
    _startLine = 0;
    QTimer::singleShot(60 * 1000, this, [this](){ this->sendUpdateSecurities(_startLine); });

    emit sendLogMsg(STOCK_ID, TDBLoger::MSG_CODE::WARNING_CODE, QString("The search for the list of securities failed with an error. Retry after 60 s"));
}

void Moex::parseSecurities(const QByteArray &answer)
{
    const auto _oldStartLine = _startLine;
    if (_startLine == 0)
    {
        _securities.clear();
    }
    try
    {
        QJsonParseError error;
        const auto docJsonObject = QJsonDocument::fromJson(answer, &error);
        if (error.error != QJsonParseError::NoError)
        {
            throw ParseException(QString("Parse json error: %1").arg(error.errorString()));
        }

        if (!docJsonObject.isObject())
        {
            throw ParseException(QString("JSON document [Root] is not object"));
        }

        const auto rootJson = docJsonObject.object();

        const auto securitiesJsonObject = rootJson["securities"];
        if (!securitiesJsonObject.isObject())
        {
            throw ParseException(QString("JSON field [Root]/[securities] is not object"));
        }

        const auto securitiesJson = securitiesJsonObject.toObject();

        //парсим названия столбцов
        const auto columnsJsonArray = securitiesJson["columns"];
        if (!columnsJsonArray.isArray())
        {
            throw ParseException(QString("JSON field [Root]/[securities]/[columns] is not array"));
        }

        std::unordered_map<quint8, QString> columnsTypes;
        quint8 index = 0;
        foreach (const auto& columnName, columnsJsonArray.toArray())
        {
            if (!columnName.isString())
            {
                throw ParseException(QString("Value of JSON field [Root]/[securities]/[columns] is not string"));
            }
            columnsTypes.emplace(index, columnName.toString());
            ++index;
        }

        const auto datasJsonArray = securitiesJson["data"];
        if (!datasJsonArray.isArray())
        {
            throw ParseException(QString("JSON field [Root]/[securities]/[data] is not array"));
        }

        qsizetype line = 1;
        foreach (const auto& dataJsonArray, datasJsonArray.toArray())
        {
            const auto path = QString("[Root]/[securities]/[data]/[] Line %1").arg(line);
            if (!dataJsonArray.isArray())
            {
                throw ParseException(QString("JSON field %1 is not array").arg(path));
            }

            auto tmp = std::make_unique<SecurityInfo>();
            const auto dataArray = dataJsonArray.toArray();
            for (quint8 i = 0; i < dataArray.size() && i < columnsTypes.size(); ++i)
            {
                const auto dataJson = dataArray.at(i);
                const auto& columnType = columnsTypes.at(i);

                if (columnType == "id")
                {
                    const auto value = JSONReadNumber(dataJson, path, 1);
                    if (!value.has_value())
                    {
                        throw ParseException(QString("Value in column %1 on path %2 is null").arg(columnType).arg(path));
                    }
                    tmp->id = value.value();
                }
                else if (columnType == "secid")
                {
                    const auto value = JSONReadString(dataJson, path, false);
                    if (!value.has_value())
                    {
                        throw ParseException(QString("Value in column %1 on path %2 is null").arg(columnType).arg(path));
                    }
                    tmp->secid = value.value();
                }
                else if (columnType == "shortname")
                {
                    const auto value = JSONReadString(dataJson, path, false);
                    tmp->shortname = value.has_value() ? value.value() : QString();
                }
                else if (columnType == "regnumber")
                {
                    const auto value = JSONReadString(dataJson, path);
                    tmp->regnumber = value.has_value() ? value.value() : QString();
                }
                else if (columnType == "name")
                {
                    const auto value = JSONReadString(dataJson, path, false);
                    tmp->name = value.has_value() ? value.value() : QString();
                }
                else if (columnType == "isin")
                {
                    const auto value = JSONReadString(dataJson, path);
                    tmp->isin = value.has_value() ? value.value() : QString();
                }
                else if (columnType == "is_traded")
                {
                    const auto value = JSONReadNumber<quint8>(dataJson, path, 0, 1);
                    tmp->is_traded = value.has_value() ? value.value() : 0;
                }
                else if (columnType == "emitent_id")
                {
                    const auto value = JSONReadNumber<qint32>(dataJson, path);
                    tmp->emitent_id = value.has_value() ? value.value() : 0;
                }
                else if (columnType == "emitent_title")
                {
                    const auto value = JSONReadString(dataJson, path);
                    tmp->emitent_title = value.has_value() ? value.value() : QString();
                }
                else if (columnType == "emitent_inn")
                {
                    const auto value = JSONReadString(dataJson, path);
                    tmp->emitent_title = value.has_value() ? value.value() : QString();
                }
                else if (columnType == "emitent_okpo")
                {
                    const auto value = JSONReadString(dataJson, path);
                    tmp->emitent_okpo = value.has_value() ? value.value() : QString();
                }
                else if (columnType == "gosreg")
                {
                    const auto value = JSONReadString(dataJson, path);
                    tmp->gosreg = value.has_value() ? value.value() : QString();
                }
                else if (columnType == "type")
                {
                    const auto value = JSONReadString(dataJson, path);
                    tmp->type = value.has_value() ? value.value() : QString();
                }
                else if (columnType == "group")
                {
                    const auto value = JSONReadString(dataJson, path, false);
                    tmp->group = value.has_value() ? value.value() : QString();
                }
                else if (columnType == "primary_boardid")
                {
                    const auto value = JSONReadString(dataJson, path);
                    tmp->primary_boardid = value.has_value() ? value.value() : QString();
                }

                ++line;
            }

            const auto secid = tmp->secid;
            _securities.emplace(std::move(secid), std::move(tmp));

            ++_startLine;
        }
    }
    catch (const ParseException& err)
    {
        emit sendLogMsg(STOCK_ID, TDBLoger::MSG_CODE::WARNING_CODE, QString("Error parse JSON securities: %1").arg(err.what()));

        restartUpdateSecurities();

        return;
    }

    if (_oldStartLine != _startLine)
    {
        sendUpdateSecurities(_startLine);
    }
    else
    {
        emit sendLogMsg(STOCK_ID, TDBLoger::MSG_CODE::INFORMATION_CODE, QString("The earch for the list of securities complite successfully"));

        _startLine = 0;

        makeKLines();
    }
}

void Moex::makeKLines()
{
    quint32 addCount = 0;
    quint32 eraseCount = 0;
    for (const auto& security: _securities)
    {
        const TradingCatCommon::KLineID klineId(security.first, TradingCatCommon::KLineType::MIN1);
        if ((security.second->primary_boardid == "TQBR") && (!_pool->isExitsKLine(klineId)))
        {
            auto kline = std::make_unique<MoexKLine>(klineId, "stock", "shares", "TQBR", QDateTime::currentDateTime().addMSecs(-(static_cast<qint64>(klineId.type) * 100)));

            _pool->addKLine(std::move(kline));

            ++addCount;
        }
    }
    for (const auto& klineId: _pool->addedKLines())
    {
        const auto it_securities = _securities.find(klineId.symbol);
        if (it_securities == _securities.end() || (it_securities != _securities.end() && it_securities->second->primary_boardid != "TQBR"))
        {
            _pool->deleteKLine(klineId);

            ++eraseCount;
        }
    }

    emit sendLogMsg(STOCK_ID, TDBLoger::MSG_CODE::INFORMATION_CODE, QString("Securities list update successfully. Added: %1. Erased: %2. Total: %3")
                                                              .arg(addCount)
                                                              .arg(eraseCount)
                                                              .arg(_pool->klineCount()));
}

