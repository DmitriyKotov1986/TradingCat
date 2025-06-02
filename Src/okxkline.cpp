//Qt
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QRandomGenerator64>

//My
#include <Common/parser.h>

#include "okxkline.h"

using namespace TradingCatCommon;
using namespace Common;

Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://www.okx.com/"));
static const quint64 MIN_REQUEST_PERION = 60 * 1000; // 5min
static const quint64 MIN_REQUEST_PERION_429 = 10 * 60 * 1000; //10 min

QString OkxKLine::KLineTypeToString(TradingCatCommon::KLineType type)
{
    switch (type)
    {
    case KLineType::MIN1: return "1m";
    case KLineType::MIN5: return "5m";
    case KLineType::MIN15: return "15m";
    case KLineType::MIN30: return "30m";
    case KLineType::MIN60: return "60m";
    case KLineType::HOUR4: return "240m";
    case KLineType::HOUR8: return "360m";
    case KLineType::DAY1: return "1D";
    case KLineType::WEEK1: return "1W";
    default:
        Q_ASSERT(false);
    }

    return "UNKNOW";
}

OkxKLine::OkxKLine(const TradingCatCommon::KLineID &id, const QDateTime& lastClose, QObject *parent /* = nullptr */)
    : IKLine(id, parent)
    , _lastClose(lastClose.addMSecs(static_cast<quint64>(IKLine::id().type) / 2).toMSecsSinceEpoch())
{
    Q_ASSERT(!id.isEmpty());
    Q_ASSERT(id.type == KLineType::MIN1 || id.type == KLineType::MIN5);
}

void OkxKLine::sendGetKline()
{
    Q_ASSERT(_currentRequestId == 0);
    Q_ASSERT(_isStarted);

    //Запрашиваем немного больше свечей чтобы компенсировать разницу часов между сервером и биржей
    //лишнии свечи отбросим при парсинге
    quint32 count = ((QDateTime::currentDateTime().toMSecsSinceEpoch() - _lastClose) / static_cast<quint64>(IKLine::id().type)) + 10;
    if (count > 1000)
    {
        count = 1000;
    }

    QUrlQuery urlQuery;
    urlQuery.addQueryItem("instId", id().symbol.name);
    urlQuery.addQueryItem("bar", KLineTypeToString(IKLine::id().type));
    urlQuery.addQueryItem("limit", QString::number(count));
//    urlQuery.addQueryItem("before", QString::number(_lastClose.toMSecsSinceEpoch()));

    QUrl url(*BASE_URL);
    url.setPath("/api/v5/market/candles");
    url.setQuery(urlQuery);

    auto http = getHTTP();
    _currentRequestId = http->send(url, Common::HTTPSSLQuery::RequestType::GET);
}

PKLinesList OkxKLine::parseKLine(const QByteArray &answer)
{
    PKLinesList result = std::make_shared<KLinesList>();

    try
    {
        const auto rootJson = JSONParseToMap(answer);
        const auto data = JSONReadMapToArray(rootJson, "data", "Root/data");
        if (data.size() < 2)
        {
            return result;
        }

        //Последняя свеча может быть некорректно сформирована, поэтму в следующий раз нам надо получить ее еще раз
        const auto it_klinePreEnd = std::prev(data.end());
        for (auto it_kline = it_klinePreEnd; it_kline != data.begin(); --it_kline)
        {
            // Start time of the candle cycle, opening price, closing price, highest price, Lowest price, Transaction amount, Transaction volume
            const auto kline = JSONReadArray(*it_kline, "Root/data/[]");

            const auto openDateTime = kline[0].toString().toLongLong();
            const auto closeDateTime = openDateTime + static_cast<qint64>(IKLine::id().type);

            //отсеиваем лишнии свечи
            if (_lastClose >= closeDateTime)
            {
                continue;
            }

            auto tmp = std::make_shared<KLine>();
            tmp->openTime = openDateTime;
            tmp->open = kline[1].toString().toFloat();
            tmp->high = kline[2].toString().toFloat();
            tmp->low = kline[3].toString().toFloat();
            tmp->close = kline[4].toString().toFloat();
            tmp->volume = kline[5].toString().toFloat();
            tmp->quoteAssetVolume = kline[6].toString().toFloat();
            tmp->closeTime = closeDateTime;
            tmp->id = IKLine::id();

            result->emplace_back(std::move(tmp));

            //Вычисляем самую позднюю свечу
            _lastClose = std::max(_lastClose, closeDateTime);
        }
    }
    catch (const ParseException& err)
    {
        result->clear();

        emit sendLogMsg(IKLine::id(), TDBLoger::MSG_CODE::WARNING_CODE, QString("Error parsing KLine: %1 Source: %2").arg(err.what()).arg(answer));

        return result;
    }

    return result;
}

void OkxKLine::getAnswerHTTP(const QByteArray &answer, quint64 id)
{
    if (id != _currentRequestId)
    {
        return;
    }

    _currentRequestId = 0;

    addKLines(parseKLine(answer));

    const auto type = static_cast<quint64>(IKLine::id().type);

    QTimer::singleShot(type * 2, this, [this](){ this->sendGetKline(); });
}

void OkxKLine::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id, const QByteArray& answer)
{
    Q_UNUSED(code);

    if (id != _currentRequestId)
    {
        return;
    }

    _currentRequestId = 0;

    emit sendLogMsg(IKLine::id(), TDBLoger::MSG_CODE::WARNING_CODE, QString("KLine %1: Error get data from HTTP server: %2").arg(IKLine::id().toString()).arg(msg));

    //429 To many request
    if (serverCode >= 400 || code == QNetworkReply::OperationCanceledError)
    {
        QTimer::singleShot(MIN_REQUEST_PERION_429, this, [this](){ this->sendGetKline(); });

        return;
    }

    const auto interval = static_cast<qint64>(IKLine::id().type);
    QTimer::singleShot(MIN_REQUEST_PERION + interval + QRandomGenerator64::global()->bounded(interval), this,
                       [this](){ this->sendGetKline(); });
}

void OkxKLine::start()
{
    auto http = getHTTP();

    Q_CHECK_PTR(http);

    QObject::connect(http, SIGNAL(getAnswer(const QByteArray&, quint64)),
                     SLOT(getAnswerHTTP(const QByteArray&, quint64)));
    QObject::connect(http, SIGNAL(errorOccurred(QNetworkReply::NetworkError, quint64, const QString&, quint64, const QByteArray&)),
                     SLOT(errorOccurredHTTP(QNetworkReply::NetworkError, quint64, const QString&, quint64, const QByteArray&)));
    QObject::connect(http, SIGNAL(sendLogMsg(Common::TDBLoger::MSG_CODE, const QString&, quint64)),
                     SLOT(sendLogMsgHTTP(Common::TDBLoger::MSG_CODE, const QString&, quint64)));

    _isStarted = true;

    sendGetKline();
}

void OkxKLine::stop()
{
    if (!_isStarted)
    {
        return;
    }

    _isStarted = false;
}

void OkxKLine::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    Q_UNUSED(id);

    emit sendLogMsg(IKLine::id(), category, msg);
}
