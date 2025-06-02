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

#include "bitmartkline.h"

using namespace TradingCatCommon;
using namespace Common;

Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://api-cloud.bitmart.com/"));
static const quint64 MIN_REQUEST_PERION = 60 * 1000; // 5min
static const quint64 MIN_REQUEST_PERION_429 = 10 * 60 * 1000; //10 min

QString BitmartKLine::KLineTypeToString(TradingCatCommon::KLineType type)
{
    // [1, 5, 15, 30, 60, 120, 240, 1440, 10080, 43200]
    switch (type)
    {
    case KLineType::MIN1: return "1";
    case KLineType::MIN5: return "5";
    case KLineType::MIN15: return "15";
    case KLineType::MIN30: return "30";
    case KLineType::MIN60: return "60";
    case KLineType::HOUR4: return "240";
    case KLineType::HOUR8: return "1440";
    case KLineType::DAY1: return "10080";
    case KLineType::WEEK1: return "43200";
    default:
        Q_ASSERT(false);
    }

    return "UNKNOW";
}

BitmartKLine::BitmartKLine(const TradingCatCommon::KLineID &id, const QDateTime& lastClose, QObject *parent /* = nullptr */)
    : IKLine(id, parent)
    , _lastClose(lastClose.addMSecs(static_cast<quint64>(IKLine::id().type) / 2).toMSecsSinceEpoch())
{
    Q_ASSERT(!id.isEmpty());
    Q_ASSERT(id.type == KLineType::MIN1 || id.type == KLineType::MIN5);
}

void BitmartKLine::sendGetKline()
{
    Q_ASSERT(_currentRequestId == 0);
    Q_ASSERT(_isStarted);

    //Запрашиваем немного больше свечей чтобы компенсировать разницу часов между сервером и биржей
    //лишнии свечи отбросим при парсинге
    quint32 count = ((QDateTime::currentDateTime().toMSecsSinceEpoch() - _lastClose) / static_cast<quint64>(IKLine::id().type)) + 10;
    if (count > 200)
    {
        count = 200;
    }

    QUrlQuery urlQuery;
    urlQuery.addQueryItem("symbol", id().symbol.name);
    urlQuery.addQueryItem("step", KLineTypeToString(IKLine::id().type));
    //urlQuery.addQueryItem("after", QString::number(_lastClose / 1000));
    urlQuery.addQueryItem("limit", QString::number(count));

    QUrl url(*BASE_URL);
    url.setPath("/spot/quotation/v3/lite-klines");
    url.setQuery(urlQuery);

    auto http = getHTTP();
    _currentRequestId = http->send(url, Common::HTTPSSLQuery::RequestType::GET);
}

PKLinesList BitmartKLine::parseKLine(const QByteArray &answer)
{
    PKLinesList result = std::make_shared<KLinesList>();

    try
    {
        const auto rootJson = JSONParseToMap(answer);

        const auto code = JSONReadMapNumber<qint64>(rootJson, "code", "Root/code").value_or(1000); //1000 is OK code
        if (code != 1000)
        {
            const auto msg = JSONReadMapString(rootJson, "message", "Root/message").value_or("");

            throw ParseException(QString("Stock exchange return error. Code: %1 Message: %2").arg(code).arg(msg));
        }

        const auto data = JSONReadMapToArray(rootJson, "data", "Root/data");

        if (data.size() < 2)
        {
            return result;
        }

        //Последняя свеча может быть некорректно сформирована, поэтму в следующий раз нам надо получить ее еще раз
        const auto it_dataPreEnd = std::prev(data.end());
        for (auto it_data = data.begin(); it_data != it_dataPreEnd; ++it_data)
        {
            const auto data = JSONReadArray(*it_data, "Root/data/[]");

            const auto openDateTime = data[0].toString().toLongLong() * 1000;
            const auto closeDateTime = openDateTime + static_cast<qint64>(IKLine::id().type);

            //отсеиваем лишнии свечи
            if (_lastClose >= closeDateTime)
            {
                continue;
            }

            auto tmp = std::make_shared<KLine>();
            tmp->openTime = openDateTime;
            tmp->open = data[1].toString().toFloat();
            tmp->high = data[2].toString().toFloat();
            tmp->low = data[3].toString().toFloat();
            tmp->close = data[4].toString().toFloat();
            tmp->volume = data[5].toString().toFloat();
            tmp->quoteAssetVolume = data[6].toString().toFloat();
            tmp->closeTime = closeDateTime;
            tmp->id = IKLine::id();

            // qWarning() << "Bitmart: " << tmp->openTime <<
            //     QDateTime::fromMSecsSinceEpoch(tmp->openTime).toString("hh:mm") <<
            //     tmp->id.toString() <<
            //     tmp->deltaKLine() <<
            //     tmp->volumeKLine() <<
            //     "O:" << tmp->open <<
            //     "H:" << tmp->high <<
            //     "L:" << tmp->low <<
            //     "C:" << tmp->close;

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

void BitmartKLine::getAnswerHTTP(const QByteArray &answer, quint64 id)
{
    if (id != _currentRequestId)
    {
        return;
    }

    addKLines(parseKLine(answer));

    _currentRequestId = 0;

    const auto type = static_cast<quint64>(IKLine::id().type);

    QTimer::singleShot(type * 2, this, [this](){ this->sendGetKline(); });
}

void BitmartKLine::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id, const QByteArray& answer)
{
    Q_UNUSED(code);

    if (id != _currentRequestId)
    {
        return;
    }

    _currentRequestId = 0;

    if (!answer.isEmpty())
    {
        try
        {
            const auto rootJson = JSONParseToMap(answer);

            const auto errCode = JSONReadMapNumber<qint64>(rootJson, "code", "Root/code").value_or(1000); //1000 is OK code
            if (errCode  != 1000)
            {
                const auto msg = JSONReadMapString(rootJson, "message", "Root/message").value_or("");

                emit sendLogMsg(IKLine::id(), Common::TDBLoger::MSG_CODE::WARNING_CODE, QString("Stock exchange return error. Code: %1 Message: %2").arg(code).arg(msg));
            }
        }
        catch (const ParseException& err)
        {
            emit sendLogMsg(IKLine::id(), TDBLoger::MSG_CODE::WARNING_CODE, QString("Error parse JSON error data: %1. Source data: %2").arg(err.what()).arg(answer));
        }
    }

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

void BitmartKLine::start()
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

void BitmartKLine::stop()
{
    if (!_isStarted)
    {
        return;
    }

    _isStarted = false;
}

void BitmartKLine::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    Q_UNUSED(id);

    emit sendLogMsg(IKLine::id(), category, msg);
}
