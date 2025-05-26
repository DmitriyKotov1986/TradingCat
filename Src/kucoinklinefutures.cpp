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

#include "kucoinklinefutures.h"

using namespace TradingCatCommon;
using namespace Common;

Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://api-futures.kucoin.com/"));
static const quint64 MIN_REQUEST_PERION = 60 * 1000; // 5min
static const quint64 MIN_REQUEST_PERION_429 = 10 * 60 * 1000; //10 min

QString KucoinKLineFutures::KLineTypeToString(TradingCatCommon::KLineType type)
{
    switch (type)
    {
    case KLineType::MIN1: return "1";
    case KLineType::MIN5: return "5";
    case KLineType::MIN15: return "15";
    case KLineType::MIN30: return "30";
    case KLineType::MIN60: return "60";
    case KLineType::HOUR4: return "240";
    case KLineType::HOUR8: return "480";
    case KLineType::DAY1: return "1440";
    case KLineType::WEEK1: return "10080";
    default:
        Q_ASSERT(false);
    }

    return "UNKNOW";
}

KucoinKLineFutures::KucoinKLineFutures(const TradingCatCommon::KLineID &id, const QDateTime& lastClose, QObject *parent /* = nullptr */)
    : IKLine(id, parent)
    , _lastClose(lastClose.addMSecs(static_cast<qint64>(IKLine::id().type) / 2).toMSecsSinceEpoch())
{
    Q_ASSERT(!id.isEmpty());
    Q_ASSERT(id.type == KLineType::MIN1 || id.type == KLineType::MIN5);
}

void KucoinKLineFutures::sendGetKline()
{
    Q_ASSERT(_currentRequestId == 0);
    Q_ASSERT(_isStarted);

    //Запрашиваем немного больше свечей чтобы компенсировать разницу часов между сервером и биржей
    //лишнии свечи отбросим при парсинге
    quint32 count = ((QDateTime::currentDateTime().toMSecsSinceEpoch() - _lastClose) / static_cast<quint64>(IKLine::id().type)) + 10;
    if (count > 500)
    {
        count = 500;
    }

    QUrlQuery urlQuery;
    urlQuery.addQueryItem("symbol", IKLine::id().symbol);
    urlQuery.addQueryItem("granularity", KLineTypeToString(IKLine::id().type));
    urlQuery.addQueryItem("from", QString::number(_lastClose - static_cast<quint64>(IKLine::id().type) * 10));

    QUrl url(*BASE_URL);
    url.setPath("/api/v1/kline/query");
    url.setQuery(urlQuery);

    auto http = getHTTP();
    _currentRequestId = http->send(url, Common::HTTPSSLQuery::RequestType::GET);
}

PKLinesList KucoinKLineFutures::parseKLine(const QByteArray &answer)
{
    PKLinesList result = std::make_shared<KLinesList>();

    try
    {
        const auto rootJson = JSONParseToMap(answer);
        const auto dataJson = JSONReadMapToArray(rootJson, "data", "Root/data");

        if (dataJson.size() < 2)
        {
            return result;
        }

        //Последняя свеча может быть некорректно сформирована, поэтму в следующий раз нам надо получить ее еще раз
        const auto it_klinePreEnd = std::prev(dataJson.end());
        for (auto it_kline = dataJson.begin(); it_kline !=it_klinePreEnd; ++it_kline)
        {
            // Start time of the candle cycle, opening price, closing price, highest price, Lowest price, Transaction amount, Transaction volume
            const auto data = JSONReadArray(*it_kline, "Root/data/[]");

            const auto openDateTime = data[0].toInteger();
            const auto closeDateTime = openDateTime + static_cast<qint64>(IKLine::id().type);

            //отсеиваем лишнии свечи
            if (_lastClose >= closeDateTime)
            {
                continue;
            }

            auto tmp = std::make_shared<KLine>();
            tmp->openTime = openDateTime;
            tmp->open = data[1].toDouble();
            tmp->high = data[2].toDouble();
            tmp->low = data[3].toDouble();
            tmp->close = data[4].toDouble();
            tmp->volume = data[5].toDouble();
            tmp->quoteAssetVolume = tmp->volume;
            tmp->closeTime = closeDateTime;
            tmp->id = IKLine::id();

            // qWarning() << "Kucoin futures delta volume:" << tmp->openTime <<
            //     QDateTime::fromMSecsSinceEpoch(tmp->openTime).toString("hh:mm") <<
            //     tmp->id.toString() <<
            //     tmp->deltaKLine() <<
            //     tmp->volumeKLine() <<
            //     "O:" << tmp->open <<
            //     "H:" << tmp->high <<
            //     "L:" << tmp->low <<
            //     "C:" << tmp->close <<
            //     "V:" << tmp->volume;

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

void KucoinKLineFutures::getAnswerHTTP(const QByteArray &answer, quint64 id)
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

void KucoinKLineFutures::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id, const QByteArray& answer)
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

void KucoinKLineFutures::start()
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

void KucoinKLineFutures::stop()
{
    if (!_isStarted)
    {
        return;
    }

    _isStarted = false;
}

void KucoinKLineFutures::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    Q_UNUSED(id);

    emit sendLogMsg(IKLine::id(), category, msg);
}
