#pragma once

//Qt
#include <QObject>
#include <QDateTime>
#include <QByteArray>

//My
#include <TradingCatCommon/ikline.h>
#include <TradingCatCommon/stockexchange.h>

class MexcKLine
    : public TradingCatCommon::IKLine
{
    Q_OBJECT

public:
    MexcKLine(const TradingCatCommon::KLineID& id, const QDateTime& lastClose, const QString& secretKey = QString(), QObject* parent = nullptr);

    void start() override;
    void stop() override;

private:
    MexcKLine() = delete;
    Q_DISABLE_COPY_MOVE(MexcKLine);

    static QString KLineTypeToString(TradingCatCommon::KLineType type);

    void sendGetKline();
    TradingCatCommon::PKLinesList parseKLine(const QByteArray& answer);

private slots:
    void getAnswerHTTP(const QByteArray& answer, quint64 id);
    void errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString& msg, quint64 id, const QByteArray& answer);
    void sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString& msg, quint64 id);

private:
    quint64 _currentRequestId = 0;
    bool _isStarted = false;

    qint64 _lastClose = 0;

    const QString _secretKey;

};
