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

#include "mexckline.h"

using namespace TradingCatCommon;
using namespace Common;

Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://api.mexc.com/"));
static const quint64 MIN_REQUEST_PERION = 60 * 1000; // 5min
static const quint64 MIN_REQUEST_PERION_429 = 10 * 60 * 1000; //10 min

QString MexcKLine::KLineTypeToString(TradingCatCommon::KLineType type)
{
    return TradingCatCommon::KLineTypeToString(type);
}

MexcKLine::MexcKLine(const TradingCatCommon::KLineID &id, const QDateTime& lastClose, const QString& secretKey /* =QString() */, QObject *parent /* = nullptr */)
    : IKLine(id, parent)
    , _lastClose(lastClose.toMSecsSinceEpoch())
    , _secretKey(secretKey)
{
    Q_ASSERT(!id.isEmpty());
    Q_ASSERT(id.type == KLineType::MIN1 || id.type == KLineType::MIN5);
}

void MexcKLine::sendGetKline()
{
    Q_ASSERT(_currentRequestId == 0);
    Q_ASSERT(_isStarted);

    //Запрашиваем немного больше свечей чтобы компенсировать разницу часов между сервером и биржей
    //лишнии свечи отбросим при парсинге
    quint32 count = ((QDateTime::currentDateTime().toMSecsSinceEpoch() - _lastClose) / static_cast<quint64>(IKLine::id().type)) + 10;
    if (count > 999)
    {
        count = 999;
    }

    QUrlQuery urlQuery;
    urlQuery.addQueryItem("symbol", IKLine::id().symbol);
    urlQuery.addQueryItem("interval", KLineTypeToString(id().type));
    urlQuery.addQueryItem("limit", QString::number(count));

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
    url.setPath("/api/v3/klines");
    url.setQuery(urlQuery);

    auto http = getHTTP();
    _currentRequestId = http->send(url, Common::HTTPSSLQuery::RequestType::GET);
}

PKLinesList MexcKLine::parseKLine(const QByteArray &answer)
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
            tmp->open = data[1].toString().toDouble();
            tmp->high = data[2].toString().toDouble();
            tmp->low = data[3].toString().toDouble();
            tmp->close = data[4].toString().toDouble();
            tmp->volume = data[5].toString().toDouble();
            tmp->closeTime = closeDateTime;
            tmp->quoteAssetVolume = data[7].toString().toDouble();
            tmp->id = IKLine::id();

            result->emplace_back(std::move(tmp));

            //Вычисляем самую позднюю свечу
            _lastClose = std::max(_lastClose, closeDateTime);
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

void MexcKLine::getAnswerHTTP(const QByteArray &answer, quint64 id)
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

void MexcKLine::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id)
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

void MexcKLine::start()
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

void MexcKLine::stop()
{
    if (!_isStarted)
    {
        return;
    }

    _isStarted = false;
}

void MexcKLine::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    Q_UNUSED(id);

    emit sendLogMsg(IKLine::id(), category, msg);
}
