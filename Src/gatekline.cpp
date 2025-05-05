//STL
#include <algorithm>

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

#include "gatekline.h"

using namespace TradingCatCommon;
using namespace Common;

Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://api.gateio.ws/"));
static const quint64 MIN_REQUEST_PERION = 60 * 1000; // 1min
static const quint64 MIN_REQUEST_PERION_429 = 10 * 60 * 1000; //10 min

QString GateKLine::KLineTypeToString(TradingCatCommon::KLineType type)
{
    switch (type)
    {
    case KLineType::MIN1: return "1m";
    case KLineType::MIN5: return "5m";
    case KLineType::MIN15: return "15m";
    case KLineType::MIN30: return "30m";
    case KLineType::MIN60: return "1h";
    case KLineType::HOUR4: return "4h";
    case KLineType::HOUR8: return "8h";
    case KLineType::DAY1: return "1d";
    case KLineType::WEEK1: return "1w";
    default:
        Q_ASSERT(false);
    }

    return "UNKNOW";
}

GateKLine::GateKLine(const TradingCatCommon::KLineID &id, const QDateTime& lastClose, QObject *parent /* = nullptr */)
    : IKLine(id, parent)
    , _lastClose(lastClose)
{
    Q_ASSERT(!id.isEmpty());
    Q_ASSERT(id.type == KLineType::MIN1 || id.type == KLineType::MIN5);
}

void GateKLine::sendGetKline()
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
    urlQuery.addQueryItem("currency_pair", IKLine::id().symbol);
    urlQuery.addQueryItem("interval", KLineTypeToString(IKLine::id().type));
    urlQuery.addQueryItem("limit", QString::number(count));

    QUrl url(*BASE_URL);
    url.setPath("/api/v4/spot/candlesticks");
    url.setQuery(urlQuery);

    auto http = getHTTP();
    _currentRequestId = http->send(url, Common::HTTPSSLQuery::RequestType::GET);
}

PKLinesList GateKLine::parseKLine(const QByteArray &answer)
{
    PKLinesList result = std::make_shared<KLinesList>();

    try
    {
        const auto arrayJson = JSONParseToArray(answer);

        for (const auto& kline: arrayJson)
        {
            const auto data = kline.toArray();

            const auto openDateTime = QDateTime::fromSecsSinceEpoch(data[0].toString().toLongLong());
            const auto closeDateTime = openDateTime.addMSecs(static_cast<quint64>(IKLine::id().type));

            auto tmp = std::make_shared<KLine>();
            tmp->openTime = openDateTime;
            tmp->quoteAssetVolume = data[1].toString().toDouble();
            tmp->close = data[2].toString().toDouble();
            tmp->high = data[3].toString().toDouble();
            tmp->low = data[4].toString().toDouble();
            tmp->open = data[5].toString().toDouble();
            tmp->volume = data[6].toString().toDouble();
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
        if (result->size() > 1)
        {
            result->sort(
                [](const auto& kline1, const auto& kline2)
                {
                    return kline1->closeTime < kline2->closeTime;
                });

            //Последняя свеча может быть некорректно сформирована
            result->pop_back();

            if (!result->empty())
            {
                QDateTime maxDateTime = QDateTime::currentDateTime().addYears(-100);
                for (const auto& kline: *result)
                {
                    maxDateTime = std::max(maxDateTime, kline->closeTime);
                }

                _lastClose = maxDateTime;
            }
        }
    }
    catch (const ParseException& err)
    {
        emit sendLogMsg(IKLine::id(), TDBLoger::MSG_CODE::WARNING_CODE, QString("Error parsing KLine: %1").arg(err.what()));
        return {};
    }

    return result;
}

void GateKLine::getAnswerHTTP(const QByteArray &answer, quint64 id)
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

void GateKLine::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id)
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

void GateKLine::start()
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

void GateKLine::stop()
{
    if (!_isStarted)
    {
        return;
    }

    _isStarted = false;
}

void GateKLine::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    Q_UNUSED(id);

    emit sendLogMsg(IKLine::id(), category, msg);
}
