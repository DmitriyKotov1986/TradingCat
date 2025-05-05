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

#include "okxkline.h"

#include "okx.h"

using namespace Common;
using namespace TradingCatCommon;

//static
Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://www.okx.com/"));

const StockExchangeID Okx::STOCK_ID("OKX");

///////////////////////////////////////////////////////////////////////////////
/// class Okx
///
Okx::Okx(const TradingCatCommon::StockExchangeConfig& config, const Common::HTTPSSLQuery::ProxyList& proxyList, QObject *parent)
    : IStockExchange{STOCK_ID, parent}
    , _config(config)
    , _proxyList(proxyList)
{
    _headers.insert(QByteArray{"Content-Type"}, QByteArray{"application/json"});
}

Okx::~Okx()
{
    stop();
}

void Okx::start()
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

    sendUpdateMoney();
}

void Okx::stop()
{
    if (!_isStarted)
    {
        emit finished();

        return;
    }

    delete _pool;
    delete _http;

    _isStarted = false;

    emit finished();
}

void Okx::getAnswerHTTP(const QByteArray &answer, quint64 id)
{
    if (id != _currentRequestId)
    {
        return;
    }

    _currentRequestId = 0;

    parseMoney(answer);
}

void Okx::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id)
{
    Q_UNUSED(code);
    Q_UNUSED(serverCode);

    if (id != _currentRequestId)
    {
        return;
    }
    _currentRequestId = 0;

    emit sendLogMsg(STOCK_ID, Common::TDBLoger::MSG_CODE::WARNING_CODE, QString("HTTP request %1 failed with an error: %2").arg(id).arg(msg));

    restartUpdateMoney();
}

void Okx::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    emit sendLogMsg(STOCK_ID, category, QString("HTTP request %1: %2").arg(id).arg(msg));
}

void Okx::getKLinesPool(const TradingCatCommon::PKLinesList &klines)
{
    Q_ASSERT(!klines->empty());

#ifdef QT_DEBUG
    const auto currDateTime = QDateTime::currentDateTime();
    qint64 start = std::numeric_limits<qint64>().max();
    qint64 end = std::numeric_limits<qint64>().min();
    for (const auto& kline: *klines)
    {
        start = std::min(start, kline->closeTime);
        end = std::max(end, kline->closeTime);
    }

    emit sendLogMsg(STOCK_ID, Common::TDBLoger::MSG_CODE::INFORMATION_CODE, QString("Get new klines: %1. Count: %2 from %3 to %4")
                                                                                .arg(klines->begin()->get()->id.toString())
                                                                                .arg(klines->size())
                                                                                .arg(QDateTime::fromMSecsSinceEpoch(start).toString(SIMPLY_DATETIME_FORMAT))
                                                                                .arg(QDateTime::fromMSecsSinceEpoch(end).toString(SIMPLY_DATETIME_FORMAT)));
#endif

    emit getKLines(STOCK_ID, klines);
}


void Okx::errorOccurredPool(Common::EXIT_CODE errorCode, const QString &errorString)
{
    emit errorOccurred(STOCK_ID, errorCode, QString("KLines Pool error: %1").arg(errorString));
}

void Okx::sendLogMsgPool(Common::TDBLoger::MSG_CODE category, const QString &msg)
{
    emit sendLogMsg(STOCK_ID, category, QString("KLines Pool: %1").arg(msg));
}


void Okx::sendUpdateMoney()
{
    Q_CHECK_PTR(_http);

    Q_ASSERT(_currentRequestId == 0);
    Q_ASSERT(_isStarted);

    QUrlQuery query;
    query.addQueryItem("instType", "SPOT");

    QUrl url(*BASE_URL);
    url.setPath("/api/v5/public/instruments");
    url.setQuery(query);

    _currentRequestId = _http->send(url, Common::HTTPSSLQuery::RequestType::GET);
}

void Okx::restartUpdateMoney()
{
    QTimer::singleShot(60 * 1000, this, [this](){ if (_isStarted) this->sendUpdateMoney(); });

    emit sendLogMsg(STOCK_ID, TDBLoger::MSG_CODE::WARNING_CODE, QString("The search for the list of KLines failed with an error. Retry after 60 s"));
}

void Okx::parseMoney(const QByteArray &answer)
{
    try
    {
        const auto rootJson = JSONParseToMap(answer);
        const auto dataJson = JSONReadMapToArray(rootJson, "data", "Root/data");
        for (const auto& symbolDataValueJson: dataJson)
        {
            const auto symbolDataJson = JSONReadMap(symbolDataValueJson, "Root/data/[]");
            const auto moneyName = JSONReadMapString(symbolDataJson, "instId", "Root/symbols/[]", false);
            if (moneyName.has_value())
            {
                const auto& moneyNameStr = moneyName.value();

                if (moneyNameStr.last(4) != "USDT")
                {
                    continue;
                }

                bool filtered = false;
                if (_config.klineNames.empty())
                {
                    filtered = true;
                }
                else
                {
                    for (const auto& filter: _config.klineNames)
                    {
                        if (moneyNameStr.indexOf(filter) == 0)
                        {
                            filtered = true;
                            break;
                        }
                    }
                }

                if (!filtered)
                {
                    continue;
                }
                _symbols.emplace_back(std::move(moneyNameStr));
            }
        }

        for (const auto& symbol: _symbols)
        {
            for (const auto& type: _config.klineTypes)
            {
                _money.emplace_back(KLineID(symbol, type));
            }
        }
    }
    catch (const ParseException& err)
    {
        emit sendLogMsg(STOCK_ID, TDBLoger::MSG_CODE::WARNING_CODE, QString("Error parse JSON money list: %1").arg(err.what()));

        restartUpdateMoney();

        return;
    }

    emit sendLogMsg(STOCK_ID, TDBLoger::MSG_CODE::INFORMATION_CODE, QString("The earch for the list of money list complite successfully"));

    makeKLines();
}

void Okx::makeKLines()
{
    quint32 addKLineCount = 0;
    quint32 eraseKLineCount = 0;

    /*    std::unordered_map<QChar, quint32> count;
    for (const auto& s: _symbols)
    {
        auto it_count = count.find(s);
        if (it_count == count.end())
        {
            count.emplace(s, 1);
        }
        else
        {
            ++it_count->second;
        }
    }
    for(const auto& [s, c]: count)
    {
        qInfo() << s << c;
    }
*/

    for (const auto& klineId: _money)
    {
        if (!_pool->isExitsKLine(klineId))
        {
            auto kline = std::make_unique<OkxKLine>(klineId, QDateTime::currentDateTime().addMSecs(-static_cast<qint64>(klineId.type) * KLINES_COUNT_HISTORY));;

            _pool->addKLine(std::move(kline));

            ++addKLineCount;
        }
    }
    for (const auto& klineId: _pool->addedKLines())
    {
        const auto it_money = std::find(_money.begin(), _money.end(), klineId);
        if (it_money == _money.end())
        {
            _pool->deleteKLine(klineId);
            ++eraseKLineCount;
        }
    }

    emit sendLogMsg(STOCK_ID, TDBLoger::MSG_CODE::INFORMATION_CODE, QString("KLines list update successfully. Added: %1. Erased: %2. Total: %3")
                                                                        .arg(addKLineCount)
                                                                        .arg(eraseKLineCount)
                                                                        .arg(_pool->klineCount()));

}

