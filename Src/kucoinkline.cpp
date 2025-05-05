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

#include "kucoinkline.h"

using namespace TradingCatCommon;
using namespace Common;

Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://api.kucoin.com/"));
static const quint64 MIN_REQUEST_PERION = 60 * 1000; // 5min
static const quint64 MIN_REQUEST_PERION_429 = 10 * 60 * 1000; //10 min

QString KucoinKLine::KLineTypeToString(TradingCatCommon::KLineType type)
{
    switch (type)
    {
    case KLineType::MIN1: return "1min";
    case KLineType::MIN5: return "5min";
    case KLineType::MIN15: return "15min";
    case KLineType::MIN30: return "30min";
    case KLineType::MIN60: return "1hour";
    case KLineType::HOUR4: return "4hour";
    case KLineType::HOUR8: return "8hour";
    case KLineType::DAY1: return "1day";
    case KLineType::WEEK1: return "1week";
    default:
        Q_ASSERT(false);
    }

    return "UNKNOW";
}

KucoinKLine::KucoinKLine(const TradingCatCommon::KLineID &id, const QDateTime& lastClose, QObject *parent /* = nullptr */)
    : IKLine(id, parent)
    , _lastClose(lastClose)
{
    Q_ASSERT(!id.isEmpty());
    Q_ASSERT(id.type == KLineType::MIN1 || id.type == KLineType::MIN5);
}

void KucoinKLine::sendGetKline()
{
    Q_ASSERT(_currentRequestId == 0);
    Q_ASSERT(_isStarted);

    //Запрашиваем немного больше свечей чтобы компенсировать разницу часов между сервером и биржей
    //лишнии свечи отбросим при парсинге
    quint32 count = (_lastClose.msecsTo(QDateTime::currentDateTime()) / static_cast<quint64>(IKLine::id().type)) + 10;
    if (count > 500)
    {
        count = 500;
    }

    QUrlQuery urlQuery;
    urlQuery.addQueryItem("symbol", IKLine::id().symbol);
    urlQuery.addQueryItem("type", KLineTypeToString(IKLine::id().type));
    urlQuery.addQueryItem("startAt", QString::number(_lastClose.toSecsSinceEpoch()));
    urlQuery.addQueryItem("endAt", QString::number(QDateTime::currentDateTime().toSecsSinceEpoch()));

    QUrl url(*BASE_URL);
    url.setPath("/api/v1/market/candles");
    url.setQuery(urlQuery);

    auto http = getHTTP();
    _currentRequestId = http->send(url, Common::HTTPSSLQuery::RequestType::GET);
}

PKLinesList KucoinKLine::parseKLine(const QByteArray &answer)
{
    PKLinesList result = std::make_shared<KLinesList>();

    try
    {
        const auto rootJson = JSONParseToMap(answer);
        const auto dataJson = JSONReadMapToArray(rootJson, "data", "Root/data");

        for (const auto& kline: dataJson)
        {
            const auto data = kline.toArray();

            const auto openDateTime = QDateTime::fromSecsSinceEpoch(data[0].toString().toLongLong());
            const auto closeDateTime = openDateTime.addMSecs(static_cast<quint64>(IKLine::id().type));

            auto tmp = std::make_shared<KLine>();
            tmp->openTime = openDateTime;
            tmp->open = data[1].toString().toDouble();
            tmp->close = data[2].toString().toDouble();
            tmp->high = data[3].toString().toDouble();
            tmp->low = data[4].toString().toDouble();
            tmp->volume = data[5].toString().toDouble();
            tmp->quoteAssetVolume = data[6].toString().toDouble();
            tmp->closeTime = closeDateTime;
            tmp->id = IKLine::id();

            //отсеиваем лишнии свечи
            if (_lastClose >= tmp->closeTime)
            {
                continue;
            }

            result->emplace_back(std::move(tmp));
        }

        //Вычисляем самую позднюю свечу
        if (!result->empty())
        {
            QDateTime maxDateTime = QDateTime::currentDateTime().addYears(-100);
            for (const auto& kline: *result)
            {
                maxDateTime = std::max(maxDateTime, kline->closeTime);
            }
            //Последняя свеча может быть некорректно сформирована, поэтму в следующий раз нам надо получить ее еще раз
            _lastClose = maxDateTime.addMSecs(-(static_cast<qint64>(IKLine::id().type)));
        }
    }
    catch (const ParseException& err)
    {
        result->clear();

        emit sendLogMsg(IKLine::id(), TDBLoger::MSG_CODE::WARNING_CODE, QString("Error parsing KLine: %1").arg(err.what()));

        return result;
    }

    return result;
}

void KucoinKLine::getAnswerHTTP(const QByteArray &answer, quint64 id)
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

void KucoinKLine::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id)
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

void KucoinKLine::start()
{
    auto http = getHTTP();

    Q_CHECK_PTR(http);

    QObject::connect(http, SIGNAL(getAnswer(const QByteArray&, quint64)),
                     SLOT(getAnswerHTTP(const QByteArray&, quint64)));
    QObject::connect(http, SIGNAL(errorOccurred(QNetworkReply::NetworkError, quint64, const QString&, quint64)),
                     SLOT(errorOccurredHTTP(QNetworkReply::NetworkError, quint64, const QString&, quint64)));
    QObject::connect(http, SIGNAL(sendLogMsg(Common::TDBLoger::MSG_CODE, const QString&, quint64)),
                     SLOT(sendLogMsgHTTP(Common::TDBLoger::MSG_CODE, const QString&, quint64)));

    _isStarted = true;

    sendGetKline();
}

void KucoinKLine::stop()
{
    if (!_isStarted)
    {
        return;
    }

    _isStarted = false;
}

void KucoinKLine::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    Q_UNUSED(id);

    emit sendLogMsg(IKLine::id(), category, msg);
}
