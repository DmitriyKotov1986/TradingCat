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

#include "moexkline.h"

using namespace TradingCatCommon;
using namespace Common;

Q_GLOBAL_STATIC_WITH_ARGS(const QUrl, BASE_URL, ("https://iss.moex.com/"));
Q_GLOBAL_STATIC_WITH_ARGS(const QString, MOEX_DATETIME_FORMAT, ("yyyy-MM-dd hh:mm:ss"));
static const quint64 MIN_REQUEST_PERION = 60 * 1000; // 5min
static const quint64 MIN_REQUEST_PERION_429 = 10 * 60 * 1000; //10 min

QString MoexKLine::KLineTypeToString(TradingCatCommon::KLineType type)
{
    switch (type)
    {
    case KLineType::MIN1: return "1";
    case KLineType::MIN5: return "5";
    default:
        Q_ASSERT(false);
    }

    return "UNKNOW";
}

MoexKLine::MoexKLine(const TradingCatCommon::KLineID &id,
                     const QString &engines,
                     const QString &markets,
                     const QString &boards,
                     const QDateTime& lastClose,
                     QObject *parent /* = nullptr */)
    : IKLine(id, parent)
    , _engines(engines)
    , _markets(markets)
    , _boards(boards)
    , _lastClose(lastClose.addMSecs(static_cast<quint64>(IKLine::id().type) / 2).toMSecsSinceEpoch())
{
    Q_ASSERT(!_engines.isEmpty());
    Q_ASSERT(!_markets.isEmpty());
    Q_ASSERT(!_boards.isEmpty());

    Q_ASSERT(id.type == KLineType::MIN1 || id.type == KLineType::MIN10);
}

void MoexKLine::sendGetKline()
{
    Q_ASSERT(_currentRequestId == 0);
    Q_ASSERT(_isStarted);

    QUrlQuery urlQuery;
    urlQuery.addQueryItem("iss.meta", "off");
    urlQuery.addQueryItem("interval", KLineTypeToString(IKLine::id().type));
    urlQuery.addQueryItem("from", QDateTime::fromMSecsSinceEpoch(_lastClose).addSecs(-1).toString(*MOEX_DATETIME_FORMAT));

    QUrl url(*BASE_URL);
    url.setPath(QString("/iss/engines/%1/markets/%2/boards/%3/securities/%4/candles.json")
                    .arg(_engines)
                    .arg(_markets)
                    .arg(_boards)
                    .arg(id().symbol.name));

    url.setQuery(urlQuery);

    auto http = getHTTP();
    _currentRequestId = http->send(url, Common::HTTPSSLQuery::RequestType::GET);
}

PKLinesList MoexKLine::parseKLine(const QByteArray &answer)
{
    PKLinesList result = std::make_shared<KLinesList>();

    try
    {
        QJsonParseError error;
        const auto docJsonObject = QJsonDocument::fromJson(answer, &error);
        if (error.error != QJsonParseError::NoError)
        {
            throw ParseException(QString("Parse json error: %1").arg(error.errorString()));
        }

        if (!docJsonObject.isObject())
        {
            throw ParseException(QString("JSON document [Root] is not object"));
        }

        const auto rootJson = docJsonObject.object();

        const auto candlesJsonObject = rootJson["candles"];
        if (!candlesJsonObject.isObject())
        {
            throw ParseException(QString("JSON field [Root]/[candles] is not object"));
        }

        const auto candlesJson = candlesJsonObject.toObject();

        const auto request_error = candlesJson["error"];
        if (!request_error.isNull() && request_error.isString())
        {
            emit sendLogMsg(id(), TDBLoger::MSG_CODE::WARNING_CODE, QString("Server return error: %1").arg(request_error.toString()));

            return {};
        }

        //парсим названия столбцов
        const auto columnsJsonArray = candlesJson["columns"];
        if (!columnsJsonArray.isArray())
        {
            throw ParseException(QString("JSON field [Root]/[candles]/[columns] is not array"));
        }

        std::unordered_map<quint8, QString> columnsTypes;
        quint8 index = 0;
        for (const auto& columnName: columnsJsonArray.toArray())
        {
            if (!columnName.isString())
            {
                throw ParseException(QString("Value of JSON field [Root]/[candles]/[columns] is not string"));
            }
            columnsTypes.emplace(index, columnName.toString());
            ++index;
        }

        const auto datasJsonArray = candlesJson["data"];
        if (!datasJsonArray.isArray())
        {
            throw ParseException(QString("JSON field [Root]/[candles]/[data] is not array"));
        }

        qsizetype line = 1;
        foreach(const auto& dataJsonArray, datasJsonArray.toArray())
        {
            const auto path = QString("[Root]/[candles]/[data]/[] Line %1").arg(line);
            if (!dataJsonArray.isArray())
            {
                throw ParseException(QString("JSON field %1 is not array").arg(path));
            }

            auto tmp = std::make_shared<KLine>();
            tmp->id = id();

            const auto dataArray = dataJsonArray.toArray();
            for (quint8 i = 0; i < dataArray.size() && i < columnsTypes.size(); ++i)
            {
                const auto dataJson = dataArray.at(i);
                const auto& columnType = columnsTypes.at(i);

                //["open", "close", "high", "low", "value", "volume", "begin", "end"],

                if (columnType == "open")
                {
                    const auto value = JSONReadNumber<double>(dataJson, path, 0.0f);
                    if (!value.has_value())
                    {
                        throw ParseException(QString("Value in column %1 on path %2 is null").arg(columnType).arg(path));
                    }
                    tmp->open = value.value();
                }
                else if (columnType == "close")
                {
                    const auto value = JSONReadNumber<double>(dataJson, path, 0.0f);
                    if (!value.has_value())
                    {
                        throw ParseException(QString("Value in column %1 on path %2 is null").arg(columnType).arg(path));
                    }
                    tmp->close = value.value();
                }
                else if (columnType == "high")
                {
                    const auto value = JSONReadNumber<double>(dataJson, path, 0.0f);
                    if (!value.has_value())
                    {
                        throw ParseException(QString("Value in column %1 on path %2 is null").arg(columnType).arg(path));
                    }
                    tmp->high = value.value();
                }
                else if (columnType == "low")
                {
                    const auto value = JSONReadNumber<double>(dataJson, path, 0.0f);
                    if (!value.has_value())
                    {
                        throw ParseException(QString("Value in column %1 on path %2 is null").arg(columnType).arg(path));
                    }
                    tmp->low = value.value();
                }
                else if (columnType == "value")
                {
                    const auto value = JSONReadNumber<double>(dataJson, path, 0.0f);
                    if (!value.has_value())
                    {
                        throw ParseException(QString("Value in column %1 on path %2 is null").arg(columnType).arg(path));
                    }
                    tmp->quoteAssetVolume = value.value();
                }
                else if (columnType == "volume")
                {
                    const auto value = JSONReadNumber<double>(dataJson, path, 0.0f);
                    if (!value.has_value())
                    {
                        throw ParseException(QString("Value in column %1 on path %2 is null").arg(columnType).arg(path));
                    }
                    tmp->volume = value.value();
                }
                else if (columnType == "begin")
                {
                    const auto value = JSONReadDateTime(dataJson, path, *MOEX_DATETIME_FORMAT);
                    if (!value.has_value())
                    {
                        throw ParseException(QString("Value in column %1 on path %2 is null").arg(columnType).arg(path));
                    }
                    tmp->openTime = value.value().toMSecsSinceEpoch();
                }
                else if (columnType == "end")
                {
                    const auto value = JSONReadDateTime(dataJson, path, *MOEX_DATETIME_FORMAT);
                    if (!value.has_value())
                    {
                        throw ParseException(QString("Value in column %1 on path %2 is null").arg(columnType).arg(path));
                    }
                    tmp->closeTime = value.value().toMSecsSinceEpoch();

                }
            }

            if (tmp->closeTime > _lastClose)
            {
                _lastClose = tmp->closeTime;

                result->emplace_back(std::move(tmp));
            }
            else
            {
                emit sendLogMsg(id(), TDBLoger::MSG_CODE::WARNING_CODE,
                                QString("The KLine received is earlier than the existing one. Received: %1 Existing: %2")
                                    .arg(QDateTime::fromMSecsSinceEpoch(tmp->closeTime).toString(DATETIME_FORMAT))
                                    .arg(QDateTime::fromMSecsSinceEpoch(_lastClose).toString(DATETIME_FORMAT)));
            }

            ++line;
        }

        //Последняя свеча может быть некорректно сформирована, поэтму в следующий раз нам надо получить ее еще раз
        _lastClose = _lastClose - static_cast<qint64>(IKLine::id().type);
    }
    catch (const ParseException& err)
    {
        result->clear();

        emit sendLogMsg(id(), TDBLoger::MSG_CODE::WARNING_CODE, QString("Error parse JSON candles: %1. Data: %2").arg(err.what()).arg(answer));

        return result;
    }

    return result;
}

void MoexKLine::getAnswerHTTP(const QByteArray &answer, quint64 id)
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

void MoexKLine::errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString &msg, quint64 id, const QByteArray& answer)
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

void MoexKLine::start()
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

void MoexKLine::stop()
{
    if (!_isStarted)
    {
        return;
    }

    _isStarted = false;
}

void MoexKLine::sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString &msg, quint64 id)
{
    Q_UNUSED(id);

    emit sendLogMsg(IKLine::id(), category, msg);
}
