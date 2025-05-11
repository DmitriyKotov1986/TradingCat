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

#include "gateklinefutures.h"

using namespace TradingCatCommon;
using namespace Common;

Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://api.gateio.ws/"));
static const quint64 MIN_REQUEST_PERION = 60 * 1000; // 1min
static const quint64 MIN_REQUEST_PERION_429 = 10 * 60 * 1000; //10 min

QString GateKLineFutures::KLineTypeToString(TradingCatCommon::KLineType type)
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

GateKLineFutures::GateKLineFutures(const TradingCatCommon::KLineID &id, const QDateTime& lastClose, QObject *parent /* = nullptr */)
    : IKLine(id, parent)
    , _lastClose(lastClose.addMSecs(static_cast<quint64>(IKLine::id().type) / 2).toMSecsSinceEpoch())
{
    Q_ASSERT(!id.isEmpty());
    Q_ASSERT(id.type == KLineType::MIN1 || id.type == KLineType::MIN5);
}

void GateKLineFutures::sendGetKline()
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
    urlQuery.addQueryItem("contract", IKLine::id().symbol);
    urlQuery.addQueryItem("interval", KLineTypeToString(IKLine::id().type));
    urlQuery.addQueryItem("limit", QString::number(count));

    QUrl url(*BASE_URL);
    url.setPath("/api/v4/futures/usdt/candlesticks");
    url.setQuery(urlQuery);

    auto http = getHTTP();
    _currentRequestId = http->send(url, Common::HTTPSSLQuery::RequestType::GET);
}

PKLinesList GateKLineFutures::parseKLine(const QByteArray &answer)
{
    PKLinesList result = std::make_shared<KLinesList>();

    try
    {
        const auto arrayJson = JSONParseToArray(answer);

        if (arrayJson.size() < 2)
        {
            return result;
        }

        //Последняя свеча может быть некорректно сформирована, поэтму в следующий раз нам надо получить ее еще раз
        const auto it_klinePreEnd = std::prev(arrayJson.end());
        for (auto it_kline = arrayJson.begin(); it_kline != it_klinePreEnd; ++it_kline)
        {
            const auto data = JSONReadMap(*it_kline, "Root/[]");

            const auto openDateTime = JSONReadMapNumber<qint64>(data, "t", "Root/[]/t", 0).value_or(0) * 1000;
            const auto closeDateTime = openDateTime + static_cast<qint64>(IKLine::id().type);

            //отсеиваем лишнии свечи
            if (_lastClose >= closeDateTime)
            {
                continue;
            }

            auto tmp = std::make_shared<KLine>();
            tmp->openTime = openDateTime;
            tmp->quoteAssetVolume = JSONReadMapNumber<float>(data, "sum", "Root/[]/sum", 0.0f).value_or(0.0f);
            tmp->close = JSONReadMapNumber<float>(data, "c", "Root/[]/c", 0.0f).value_or(0.0f);
            tmp->high = JSONReadMapNumber<float>(data, "h", "Root/[]/h", 0.0f).value_or(0.0f);
            tmp->low = JSONReadMapNumber<float>(data, "l", "Root/[]/l", 0.0f).value_or(0.0f);
            tmp->open = JSONReadMapNumber<float>(data, "o", "Root/[]/o", 0.0f).value_or(0.0f);
            tmp->volume = JSONReadMapNumber<float>(data, "v", "Root/[]/v", 0.0f).value_or(0.0f);
            tmp->closeTime = closeDateTime;
            tmp->id = IKLine::id();

            result->emplace_back(std::move(tmp));

            //Вычисляем самую позднюю свечу
            _lastClose = std::max(_lastClose, closeDateTime);
        }
    }
    catch (const ParseException& err)
    {
        emit sendLogMsg(IKLine::id(), TDBLoger::MSG_CODE::WARNING_CODE, QString("Error parsing KLine: %1 Source: %2").arg(err.what()).arg(answer));

        return result;
    }

    return result;
}

void GateKLineFutures::getAnswerHTTP(const QByteArray &answer, quint64 id)
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

void GateKLineFutures::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id)
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

void GateKLineFutures::start()
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

void GateKLineFutures::stop()
{
    if (!_isStarted)
    {
        return;
    }

    _isStarted = false;
}

void GateKLineFutures::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    Q_UNUSED(id);

    emit sendLogMsg(IKLine::id(), category, msg);
}
