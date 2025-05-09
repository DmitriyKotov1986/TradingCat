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

#include "htxkline.h"

using namespace TradingCatCommon;
using namespace Common;

Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://api-aws.huobi.pro"));
static const quint64 MIN_REQUEST_PERION = 60 * 1000; // 5min
static const quint64 MIN_REQUEST_PERION_429 = 10 * 60 * 1000; //10 min

QString HtxKLine::KLineTypeToString(TradingCatCommon::KLineType type)
{
    //1min, 5min, 15min, 30min, 60min, 4hour, 1day, 1mon, 1week, 1year
    switch (type)
    {
    case KLineType::MIN1: return "1min";
    case KLineType::MIN5: return "5min";
    case KLineType::MIN15: return "15min";
    case KLineType::MIN30: return "30min";
    case KLineType::MIN60: return "60min";
    case KLineType::HOUR4: return "4hour";
    case KLineType::HOUR8: return "UNKNOW";
    case KLineType::DAY1: return "1day";
    case KLineType::WEEK1: return "1Week";
    default:
        Q_ASSERT(false);
    }

    return "UNKNOW";
}

HtxKLine::HtxKLine(const TradingCatCommon::KLineID &id, const QDateTime& lastClose, QObject *parent /* = nullptr */)
    : IKLine(id, parent)
    , _lastClose(lastClose.toMSecsSinceEpoch())
{
    Q_ASSERT(!id.isEmpty());
    Q_ASSERT(id.type == KLineType::MIN1 || id.type == KLineType::MIN5);
}

void HtxKLine::sendGetKline()
{
    Q_ASSERT(_currentRequestId == 0);
    Q_ASSERT(_isStarted);

    //Запрашиваем немного больше свечей чтобы компенсировать разницу часов между сервером и биржей
    //лишнии свечи отбросим при парсинге
    quint32 count = ((QDateTime::currentDateTime().toMSecsSinceEpoch() - _lastClose) / static_cast<quint64>(IKLine::id().type)) + 10;
    if (count > 2000)
    {
        count = 2000;
    }

    QUrlQuery urlQuery;
    urlQuery.addQueryItem("symbol", id().symbol.toLower());
    urlQuery.addQueryItem("period", KLineTypeToString(IKLine::id().type));
    urlQuery.addQueryItem("size", QString::number(count));

    QUrl url(*BASE_URL);
    url.setPath("/market/history/kline");
    url.setQuery(urlQuery);

    auto http = getHTTP();
    _currentRequestId = http->send(url, Common::HTTPSSLQuery::RequestType::GET);
}

PKLinesList HtxKLine::parseKLine(const QByteArray &answer)
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
        const auto it_dataPreEnd = std::prev(data.end());
        for (auto it_data = it_dataPreEnd; it_data != data.begin(); --it_data)
        {
            const auto kline = JSONReadMap(*it_data, "Root/data/[]");

            const auto openDateTime = JSONReadMapNumber<qint64>(kline, "id", "Root/data/[]/ts", 0).value_or(0) * 1000;
            const auto closeDateTime = openDateTime + static_cast<qint64>(IKLine::id().type);

            //отсеиваем лишнии свечи
            if (_lastClose >= closeDateTime)
            {
                continue;
            }

            auto tmp = std::make_shared<KLine>();
            tmp->openTime = openDateTime;
            tmp->open = JSONReadMapNumber<double>(kline, "open", "Root/data/[]/open", 0.0f).value_or(0.0f);
            tmp->high = JSONReadMapNumber<double>(kline, "high", "Root/data/[]/high", 0.0f).value_or(0.0f);
            tmp->low = JSONReadMapNumber<double>(kline, "low", "Root/data/[]/low", 0.0f).value_or(0.0f);
            tmp->close = JSONReadMapNumber<double>(kline, "close", "Root/data/[]/close", 0.0f).value_or(0.0f);
            tmp->volume =JSONReadMapNumber<double>(kline, "vol", "Root/data/[]/baseVol", 0.0f).value_or(0.0f);
            tmp->quoteAssetVolume = JSONReadMapNumber<double>(kline, "count", "Root/data/[]/quoteVol", 0.0f).value_or(0.0f);
            tmp->closeTime = closeDateTime;
            tmp->id = IKLine::id();

            // qInfo() << "Bybit futures delta volume:" << tmp->openTime <<
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

void HtxKLine::getAnswerHTTP(const QByteArray &answer, quint64 id)
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

void HtxKLine::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id)
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

void HtxKLine::start()
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

void HtxKLine::stop()
{
    if (!_isStarted)
    {
        return;
    }

    _isStarted = false;
}

void HtxKLine::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    Q_UNUSED(id);

    emit sendLogMsg(IKLine::id(), category, msg);
}
