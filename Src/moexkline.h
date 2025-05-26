#pragma once

//Qt
#include <QObject>
#include <QDateTime>
#include <QByteArray>

//My
#include <TradingCatCommon/ikline.h>
#include <TradingCatCommon/stockexchange.h>

class MoexKLine
    : public TradingCatCommon::IKLine
{
    Q_OBJECT

public:
    MoexKLine(const TradingCatCommon::KLineID& id, const QString& engines, const QString& markets, const QString& boards, const QDateTime& lastClose, QObject* parent = nullptr);

    void start() override;
    void stop() override;

private:
    MoexKLine() = delete;
    Q_DISABLE_COPY_MOVE(MoexKLine);

    static QString KLineTypeToString(TradingCatCommon::KLineType type);

    void sendGetKline();
    TradingCatCommon::PKLinesList parseKLine(const QByteArray& answer);

private slots:
    void getAnswerHTTP(const QByteArray& answer, quint64 id);
    void errorOccurredHTTP(QNetworkReply::NetworkError code, quint64 serverCode, const QString& msg, quint64 id, const QByteArray& answer);
    void sendLogMsgHTTP(Common::TDBLoger::MSG_CODE category, const QString& msg, quint64 id);

private:
    const QString _engines;
    const QString _markets;
    const QString _boards;

    quint64 _currentRequestId = 0;
    bool _isStarted = false;

    qint64 _lastClose = 0;

};
