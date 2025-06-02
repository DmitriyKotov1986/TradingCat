//STL
#include <limits>

//Qt
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator64>

//My
#include <Common/sql.h>

#include <TradingCatCommon/transmitdata.h>

#include "userscore.h"

using namespace Common;

static const qint64 CONNECTION_TIMEOUT = 60 * 1000;
static const qsizetype MAX_DETECT_EVENT = 5;

static QMutex onlineMutex;
static QMutex userDataMutex;

using namespace TradingCatCommon;

UsersCore::UsersCore(const Common::DBConnectionInfo &dbConnectionInfo, const TradingCatCommon::TradingData& tradingData, QObject *parent /* = nullptr*/)
    : QObject{parent}
    , _dbConnectionInfo(dbConnectionInfo)
    , _tradingData(tradingData)
{
    qRegisterMetaType<TradingCatCommon::StockExchangeID>("TradingCatCommon::StockExchangeID");
    qRegisterMetaType<TradingCatCommon::PKLinesList>("TradingCatCommon::PKLinesList");
    qRegisterMetaType<TradingCatCommon::UserConfig>("TradingCatCommon::UserConfig");
}

UsersCore::~UsersCore()
{
    stop();
}

QString UsersCore::login(const TradingCatCommon::LoginQuery &query)
{
    Q_CHECK_PTR(_users);

    const auto& userName =  query.user();
    const auto& password =  query.password();

    QMutexLocker userDataLocker(&userDataMutex);

     //незарегистрированный пользователь - добавляем его
    if (!_users->isUserExist(userName))
    {
        _users->newUser(userName, password);
    }

    auto& user = _users->user(userName);

    // неверный пароль
    if (user.password() != password)
    {
        emit sendLogMsg(TDBLoger::MSG_CODE::WARNING_CODE, QString("%1 Incorrect password of user: %1. User no login").arg(query.id()).arg(userName));

        return Package(StatusAnswer::ErrorCode::UNAUTHORIZED, "Incorrect password or user name").toJson();
    }

    // все ок - логиним пользователя
    SessionData sessionData;
    sessionData.user = userName;

    user.setLastLogin(sessionData.lastData);

    auto sessionId = getId();

    QMutexLocker onlineLocker(&onlineMutex);

    _onlineUsers.emplace(sessionId, std::move(sessionData));

    emit userOnline(sessionId, user.config());
    emit sendLogMsg(TDBLoger::MSG_CODE::INFORMATION_CODE, QString("%1 Connect user: %2 SessionID: %3. User login").arg(query.id()).arg(userName).arg(sessionId));

    return Package(LoginAnswer(sessionId, user.config(), *TradingCatCommon::OK_ANSWER_TEXT)).toJson();
}

QString UsersCore::logout(const TradingCatCommon::LogoutQuery &query)
{
    const auto sessionId = query.sessionId();

    QString userName;

    {
        QMutexLocker onlineLocker(&onlineMutex);

        const auto it_onlineUsers = _onlineUsers.find(sessionId);
        if (it_onlineUsers == _onlineUsers.end())
        {
            emit sendLogMsg(TDBLoger::MSG_CODE::WARNING_CODE, QString("%1 User not login. SessionID: %2. Skip").arg(query.id()).arg(sessionId));

            return Package(StatusAnswer::ErrorCode::UNAUTHORIZED).toJson();
        }

        userName = it_onlineUsers->second.user;

        _onlineUsers.erase(it_onlineUsers);
    }

    emit userOffline(sessionId);
    emit sendLogMsg(TDBLoger::MSG_CODE::INFORMATION_CODE, QString("%1 User logout. User: %1 SessionID: %2").arg(query.id()). arg(userName).arg(sessionId));

    return Package(LogoutAnswer(*OK_ANSWER_TEXT)).toJson();
}

QString UsersCore::config(const TradingCatCommon::ConfigQuery &query)
{
    const auto sessionId = query.sessionId();

    QMutexLocker onlineLocker(&onlineMutex);

    const auto it_onlineUsers = _onlineUsers.find(sessionId);
    if (it_onlineUsers == _onlineUsers.end())
    {
        emit sendLogMsg(TDBLoger::MSG_CODE::WARNING_CODE, QString("%1 User not login. SessionID: %2. Skip").arg(query.id()).arg(sessionId));

        return Package(StatusAnswer::ErrorCode::UNAUTHORIZED).toJson();
    }

    auto& sessionData = it_onlineUsers->second;
    const auto& userName = sessionData.user;
    sessionData.lastData = QDateTime::currentDateTime();
    sessionData.klinesDetectedList.clear();

    Q_ASSERT(!userName.isEmpty());

    QMutexLocker userDataLocker(&userDataMutex);

    auto& user = _users->user(userName);

    user.setConfig(query.config());

    emit userOnline(sessionId, user.config());
    emit sendLogMsg(TDBLoger::MSG_CODE::INFORMATION_CODE, QString("%1 User update config successfully. User: %2 SessionID: %3").arg(query.id()).arg(userName).arg(sessionId));

    return Package(ConfigAnswer(*OK_ANSWER_TEXT)).toJson();
}

QString UsersCore::detect(const TradingCatCommon::DetectQuery &query)
{
    const auto sessionId = query.sessionId();

    QMutexLocker onlineLocker(&onlineMutex);

    const auto it_onlineUsers = _onlineUsers.find(sessionId);
    if (it_onlineUsers == _onlineUsers.end())
    {
        emit sendLogMsg(TDBLoger::MSG_CODE::WARNING_CODE, QString("%1 User not login. SessionID: %2. Skip").arg(query.id()).arg(sessionId));

        return Package(StatusAnswer::ErrorCode::UNAUTHORIZED).toJson();
    }

    auto& sessionData = it_onlineUsers->second;
    sessionData.lastData = QDateTime::currentDateTime();
    auto& klinesDetectedList = sessionData.klinesDetectedList;

    const auto eventsCount = klinesDetectedList.detected.size();

    const auto result = Package(DetectAnswer(klinesDetectedList, *OK_ANSWER_TEXT)).toJson();

    klinesDetectedList.clear();

    onlineLocker.unlock();

    emit sendLogMsg(TDBLoger::MSG_CODE::INFORMATION_CODE, QString("%1 Send detect data. Detect %2 events. SessionID: %3").arg(query.id()).arg(eventsCount).arg(sessionId));

    return result;
}

bool UsersCore::isOnline(int sessionId) const
{
    QMutexLocker locker(&onlineMutex);

    return _onlineUsers.contains(sessionId);
}

QString UsersCore::stockExchange(const TradingCatCommon::StockExchangesQuery &query)
{
    const auto sessionId = query.sessionId();

    QMutexLocker onlineLocker(&onlineMutex);

    const auto it_onlineUsers = _onlineUsers.find(sessionId);
    if (it_onlineUsers == _onlineUsers.end())
    {
        emit sendLogMsg(TDBLoger::MSG_CODE::WARNING_CODE, QString("%1 User not login. SessionID: %2. Skip").arg(query.id()).arg(sessionId));

        return Package(StatusAnswer::ErrorCode::UNAUTHORIZED).toJson();
    }

    auto& sessionData = it_onlineUsers->second;
    sessionData.lastData = QDateTime::currentDateTime();

    emit sendLogMsg(TDBLoger::MSG_CODE::INFORMATION_CODE, QString("%1 Successfully finished. Send answer").arg(query.id()));

    return Package(StockExchangesAnswer(_tradingData.stockExcangesIdList(), *OK_ANSWER_TEXT)).toJson();
}

QString UsersCore::klinesIdList(const TradingCatCommon::KLinesIDListQuery &query)
{
    const auto sessionId = query.sessionId();

    QMutexLocker onlineLocker(&onlineMutex);

    const auto it_onlineUsers = _onlineUsers.find(sessionId);
    if (it_onlineUsers == _onlineUsers.end())
    {
        emit sendLogMsg(TDBLoger::MSG_CODE::WARNING_CODE, QString("%1 User not login. SessionID: %2. Skip").arg(query.id()).arg(sessionId));

        return Package(StatusAnswer::ErrorCode::UNAUTHORIZED).toJson();
    }

    auto& sessionData = it_onlineUsers->second;
    sessionData.lastData = QDateTime::currentDateTime();

    return Package(KLinesIDListAnswer(query.stockExchangeId(), _tradingData.getKLinesIDList(query.stockExchangeId()), *OK_ANSWER_TEXT)).toJson();
}

QStringList UsersCore::usersOnline() const
{
    QStringList result;

    QMutexLocker locker(&onlineMutex);

    for (const auto& user: _onlineUsers)
    {
        result.push_back(QString("%1(%2)").arg(user.second.user).arg(user.first));
    }

    return result;
}

void UsersCore::start()
{
    Q_ASSERT(!_isStarted);

    _users = new Users(_dbConnectionInfo, this);

    //Users
    connect(_users, SIGNAL(sendLogMsg(Common::TDBLoger::MSG_CODE, const QString&)),
            SLOT(sendLogMsgUsers(Common::TDBLoger::MSG_CODE, const QString&)));
    connect(_users, SIGNAL(errorOccurred(Common::EXIT_CODE, const QString&)),
            SLOT(errorOccurredUsers(Common::EXIT_CODE, const QString&)));

    _users->start();

    // ConnetionTimeoutTimer
    _connetionTimeoutTimer = new QTimer(this);

    QObject::connect(_connetionTimeoutTimer, SIGNAL(timeout()), SLOT(connectionTimeout()));

    _connetionTimeoutTimer->start(CONNECTION_TIMEOUT);

    _isStarted = true;
}

void UsersCore::stop()
{
    if (!_isStarted)
    {
        emit finished();

        return;
    }

    delete _connetionTimeoutTimer;
    _connetionTimeoutTimer = nullptr;

    _users->stop();
    delete _users;
    _users = nullptr;

    emit finished();

    _isStarted = false;
}


void UsersCore::sendLogMsgUsers(Common::TDBLoger::MSG_CODE category, const QString &msg)
{
    emit sendLogMsg(category, QString("Users data: %1").arg(msg));
}

void UsersCore::errorOccurredUsers(Common::EXIT_CODE errorCode, const QString &errorString)
{
    emit errorOccurred(errorCode, QString("Users data: %1").arg(errorString));
}

qint64 UsersCore::getId()
{
#ifndef QT_DEBUG
   return QRandomGenerator64::global()->bounded(static_cast<qint32>(1), std::numeric_limits<qint32>().max());
#else
    static qint32 id = 0;

    return ++id;
#endif
}

void UsersCore::connectionTimeout()
{
    QMutexLocker<QMutex> locker(&onlineMutex);

    for (auto it_onlineUser = _onlineUsers.begin(); it_onlineUser != _onlineUsers.end();)
    {
        auto& sessionData = it_onlineUser->second;
        if (sessionData.lastData.msecsTo(QDateTime::currentDateTime()) > CONNECTION_TIMEOUT)
        {
            emit userOffline(it_onlineUser->first);
            emit sendLogMsg(TDBLoger::MSG_CODE::WARNING_CODE,
                            QString("Connection timeout. SessionID: %1").arg(it_onlineUser->first));

            it_onlineUser = _onlineUsers.erase(it_onlineUser);      
        }
        else
        {
            ++it_onlineUser;
        }
    }
}

void UsersCore::klineDetect(qint64 sessionId, const TradingCatCommon::Detector::PKLineDetectData &detectData)
{
    Q_CHECK_PTR(detectData);
    Q_CHECK_PTR(detectData->history);
    Q_CHECK_PTR(detectData->reviewHistory);

    Q_ASSERT(sessionId != 0);
    Q_ASSERT(!detectData->stockExchangeId.isEmpty());
    Q_ASSERT(!detectData->history->empty());
    Q_ASSERT(!detectData->reviewHistory->empty());
    Q_ASSERT(detectData->filterActivate != Filter::FilterType::UNDETECT);

    QMutexLocker<QMutex> locker(&onlineMutex);

    auto it_onlineUser = _onlineUsers.find(sessionId);
    if (it_onlineUser == _onlineUsers.end())
    {
        return;
    }

    auto& klinesDetectedList = it_onlineUser->second.klinesDetectedList;

    if (klinesDetectedList.detected.size() > MAX_DETECT_EVENT)
    {
        klinesDetectedList.isFull = true;

        return;
    }

    klinesDetectedList.detected.emplace_back(detectData);
}
