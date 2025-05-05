#pragma once

//Qt
#include <QObject>
#include <QDateTime>
#include <QByteArray>

//My
#include <TradingCatCommon/ikline.h>
#include <TradingCatCommon/stockexchange.h>

class OkxKLine
    : public TradingCatCommon::IKLine
{
    Q_OBJECT

public:
    OkxKLine(const TradingCatCommon::KLineID& id, const QDateTime& lastClose, QObject* parent = nullptr);

    void start() override;
    void stop() override;

private:
    OkxKLine() = delete;
    Q_DISABLE_COPY_MOVE(OkxKLine);

    static QString KLineTypeToString(TradingCatCommon::KLineType type);

    void sendGetKline();
    TradingCatCommon::PKLinesList parseKLine(const QByteArray& answer);

private slots:
    void getAnswerHTTP(const QByteArray& answer, quint64 id);
    void errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString& msg, quint64 id);
    void sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString& msg, quint64 id);

private:
    quint64 _currentRequestId = 0;
    bool _isStarted = false;

    QDateTime _lastClose = QDateTime::currentDateTime().addDays(-1);

};
