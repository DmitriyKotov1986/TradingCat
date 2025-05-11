//Qt
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QRandomGenerator64>
#include <QMessageAuthenticationCode>

//My
#include <Common/parser.h>

#include "mexcklinefutures.h"

using namespace TradingCatCommon;
using namespace Common;

Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://contract.mexc.com/"));
static const quint64 MIN_REQUEST_PERION = 60 * 1000; // 5min
static const quint64 MIN_REQUEST_PERION_429 = 10 * 60 * 1000; //10 min

QString MexcKLineFutures::KLineTypeToString(TradingCatCommon::KLineType type)
{
    // Min1、Min5、Min15、Min30、Min60、Hour4、Hour8、Day1、Week1、Month1
    switch (type)
    {
    case KLineType::MIN1: return "Min1";
    case KLineType::MIN5: return "Min5";
    case KLineType::MIN15: return "Min15";
    case KLineType::MIN30: return "Min30";
    case KLineType::MIN60: return "Min60";
    case KLineType::HOUR4: return "Hour4";
    case KLineType::HOUR8: return "Hour8";
    case KLineType::DAY1: return "Day1";
    case KLineType::WEEK1: return "Month1";
    default:
        Q_ASSERT(false);
    }

    return "UNKNOW";
}

MexcKLineFutures::MexcKLineFutures(const TradingCatCommon::KLineID &id, const QDateTime& lastClose, const QString& secretKey /* =QString() */, QObject *parent /* = nullptr */)
    : IKLine(id, parent)
    , _lastClose(lastClose.addMSecs(static_cast<quint64>(IKLine::id().type) / 2).toMSecsSinceEpoch())
    , _secretKey(secretKey)
{
    Q_ASSERT(!id.isEmpty());
    Q_ASSERT(id.type == KLineType::MIN1 || id.type == KLineType::MIN5);
}

void MexcKLineFutures::sendGetKline()
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
    urlQuery.addQueryItem("interval", KLineTypeToString(id().type));
    urlQuery.addQueryItem("start", QString::number((_lastClose - static_cast<quint64>(IKLine::id().type) * 10) / 1000));

    if (!_secretKey.isEmpty())
    {
        //Для конечной точки SIGNED также требуется отправить параметр timestamp, который должен представлять собой временную метку в миллисекундах, когда запрос был создан и отправлен
        urlQuery.addQueryItem("timestamp", QString::number(QDateTime::currentDateTime().toMSecsSinceEpoch()));
        //Дополнительный параметр recvWindow может быть отправлен для указания количества миллисекунд после временной метки, в течение которых запрос действителен. Если recvWindow не отправлено, по умолчанию используется значение 5000
        urlQuery.addQueryItem("recvWindow", QString::number(30000));

        QMessageAuthenticationCode code(QCryptographicHash::Sha256);
        code.setKey(_secretKey.toUtf8());
        code.addData(urlQuery.toString().toUtf8());

        urlQuery.addQueryItem("signature", code.result().toHex());
    }

    QUrl url(*BASE_URL);
    url.setPath(QString("/api/v1/contract/kline/%1").arg(id().symbol));
    url.setQuery(urlQuery);

    auto http = getHTTP();
    _currentRequestId = http->send(url, Common::HTTPSSLQuery::RequestType::GET);
}

PKLinesList MexcKLineFutures::parseKLine(const QByteArray &answer)
{
    PKLinesList result = std::make_shared<KLinesList>();

    try
    {
        const auto answerJson = JSONParseToMap(answer);

        const auto success = JSONReadMapBool(answerJson, "success", "Root/success").value_or(true);
        if (!success)
        {
            const auto message =JSONReadMapString(answerJson, "message", "Root/message").value_or("");
            const auto code = JSONReadMapNumber<qint64>(answerJson, "code", "Root/code").value_or(0);

            throw ParseException(QString("Error processing request by server. Code: %1 Message: %2").arg(code).arg(message));
        }

        const auto dataJson = JSONReadMapToMap(answerJson, "data", "Root/data");

        const auto timeArrayJson = JSONReadMapToArray(dataJson, "time", "Root/data/time");
        const auto openArrayJson = JSONReadMapToArray(dataJson, "open", "Root/data/open");
        const auto closeArrayJson = JSONReadMapToArray(dataJson, "close", "Root/data/close");
        const auto highArrayJson = JSONReadMapToArray(dataJson, "high", "Root/data/high");
        const auto lowArrayJson = JSONReadMapToArray(dataJson, "low", "Root/data/low");
        const auto volArrayJson = JSONReadMapToArray(dataJson, "vol", "Root/data/vol");
        const auto amountArrayJson = JSONReadMapToArray(dataJson, "amount", "Root/data/amont");

        if (timeArrayJson.size() < 2)

        {
            return result;
        }

        //Последняя свеча может быть некорректно сформирована, поэтму в следующий раз нам надо получить ее еще раз
        qsizetype count = timeArrayJson.count() - 1;
        for (qsizetype i = 0; i < count; ++i)
        {
            const auto openDateTime = timeArrayJson[i].toInteger() * 1000;
            const auto closeDateTime = openDateTime + static_cast<qint64>(IKLine::id().type);

            //отсеиваем лишнии свечи
            if (_lastClose >= closeDateTime)
            {
                continue;
            }

            auto tmp = std::make_shared<KLine>();
            tmp->openTime = openDateTime;
            tmp->open = openArrayJson[i].toDouble();
            tmp->high = highArrayJson[i].toDouble();
            tmp->low = lowArrayJson[i].toDouble();
            tmp->close = closeArrayJson[i].toDouble();
            tmp->volume = volArrayJson[i].toDouble();
            tmp->closeTime = closeDateTime;
            tmp->quoteAssetVolume = amountArrayJson[i].toDouble();
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

void MexcKLineFutures::getAnswerHTTP(const QByteArray &answer, quint64 id)
{
    if (id != _currentRequestId)
    {
        return;
    }

    _currentRequestId = 0;

    addKLines(parseKLine(answer));

    const auto type = static_cast<quint64>(IKLine::id().type);

    QTimer::singleShot(type * 3, this, [this](){ this->sendGetKline(); });
}

void MexcKLineFutures::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id)
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

void MexcKLineFutures::start()
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

void MexcKLineFutures::stop()
{
    if (!_isStarted)
    {
        return;
    }

    _isStarted = false;
}

void MexcKLineFutures::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    Q_UNUSED(id);

    emit sendLogMsg(IKLine::id(), category, msg);
}
