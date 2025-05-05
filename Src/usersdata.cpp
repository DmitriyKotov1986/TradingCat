
#include "usersdata.h"

using namespace Common;

static const quint64 SAVE_USER_DATA_INTERVAL = 60 * 1000;

///////////////////////////////////////////////////////////////////////////////
///     The UserData class - данные пользователя
///
UserData::UserData(const QString &user, const QString password, const QString &configJson, const QDateTime &lastLogin)
    : _user(user)
    , _password(password)
    , _config(configJson)
    , _lastLogin(lastLogin)
{
    Q_ASSERT(!_user.isEmpty());
    Q_ASSERT(_lastLogin.isValid());

    if (_config.isError())
    {
        _errorString = _config.errorString();
    }
}

const QString &UserData::user() const noexcept
{
    return _user;
}

const QString &UserData::password() const noexcept
{
    return _password;
}

const TradingCatCommon::UserConfig &UserData::config() const noexcept
{
    return _config;
}

void UserData::setConfig(const TradingCatCommon::UserConfig &config)
{
    _config = config;
    _isChange = true;
}

const QDateTime &UserData::lastLogin() const noexcept
{
    return _lastLogin;
}

void UserData::setLastLogin(const QDateTime &lastLogin)
{
    _lastLogin = lastLogin;
    _isChange = true;
}

bool UserData::isChange() const noexcept
{
    return _isChange;
}

void UserData::clearIsChange() noexcept
{
    _isChange = false;
}

bool UserData::isError() const noexcept
{
    return !_errorString.isEmpty();
}

const QString &UserData::errorString() const noexcept
{
    return _errorString;
}

///////////////////////////////////////////////////////////////////////////////
///     The Users class - класс-контейнер работы с данными пользователей
///
Users::Users(const Common::DBConnectionInfo &dbConnectionInfo, QObject* parent /* = nullptr */)
    : QObject{parent}
    , _dbConnectionInfo(dbConnectionInfo)
{
}

bool Users::isUserExist(const QString& user) const
{
    return _users.contains(user);
}

UserData& Users::user(const QString& user)
{
    auto it_users = _users.find(user);

    return it_users->second;
}

void Users::start()
{
    try
    {
        connectToDB(_db, _dbConnectionInfo, "UsersDB");

        loadUserData();
    }
    catch (const SQLException& err)
    {
        emit errorOccurred(EXIT_CODE::SQL_NOT_CONNECT, err.what());

        return;
    }

    // SaveUserDataTimer
    _saveUserDataTimer = new QTimer(this);

    connect(_saveUserDataTimer, SIGNAL(timeout()), SLOT(saveUserDataTimerTimeout()));

    _saveUserDataTimer->start(SAVE_USER_DATA_INTERVAL);

    _isStarted = true;
}

void Users::stop()
{
    if (!_isStarted)
    {
        return;
    }

    delete _saveUserDataTimer;
    _saveUserDataTimer = nullptr;

    closeDB(_db);

    _isStarted = false;
}

void Users::saveUserDataTimerTimeout()
{
    for (auto& user: _users)
    {
        auto& userData = user.second;
        if (userData.isChange())
        {
            saveUserData(userData);
            userData.clearIsChange();
        }
    }
}

void Users::loadUserData()
{
    Q_ASSERT(_db.isOpen());

    const auto queryText =
        QString("SELECT `id`, `User`, `Password`, `Config`, `LastLogin` "
                "FROM `Users`");

    _db.transaction();
    QSqlQuery query(_db);
    query.setForwardOnly(true);

    DBQueryExecute(_db, query, queryText);

    _users.clear();
    while(query.next())
    {
        auto userName = query.value("User").toString();
        const auto user = userName;
        const auto password = query.value("Password").toString();
        const auto lastLogin = query.value("LastLogin").toDateTime();
        const auto config = query.value("Config").toString();

        UserData userData(user, password, config, lastLogin);

        if (userData.isError())
        {
            emit sendLogMsg(TDBLoger::MSG_CODE::WARNING_CODE, QString("Incorrect configuration user %1: %2. User skip").arg(userName).arg(userData.errorString()));

            continue;
        }

        _users.emplace(std::move(userName), std::move(userData));
    }

    _db.commit();

    emit sendLogMsg(TDBLoger::MSG_CODE::INFORMATION_CODE, QString("Users data load successfull. Total users: %1").arg(_users.size()));
}

void Users::saveUserData(const UserData& userData)
{
    const auto queryText =
        QString("UPDATE `Users` "
                "SET "
                "`Password` = '%1', "
                "`Config` = '%2', "
                "`LastLogin` = '%3' "
                "WHERE `User` = %4 ")
            .arg(userData.password())
            .arg(userData.config().toJson())
            .arg(userData.lastLogin().toString(DATETIME_FORMAT))
            .arg(userData.user());

    try
    {
        DBQueryExecute(_db, queryText);
    }
    catch (const SQLException& err)
    {
        emit errorOccurred(EXIT_CODE::SQL_NOT_CONNECT, connectDBErrorString(_db));

        return;
    }
}



UserData& Users::newUser(const QString &user, const QString &password)
{
    UserData userData(user, password, "", QDateTime::currentDateTime());

    auto it_users = _users.find(user);

    if (it_users != _users.end())
    {
        auto& existUserData = it_users->second;

        saveUserData(userData);

        existUserData = std::move(userData);

        emit sendLogMsg(TDBLoger::MSG_CODE::WARNING_CODE, QString("User data will be replace. User: %1").arg(user));

        return it_users->second;
    }

    it_users = _users.emplace(user, std::move(userData)).first;

    auto& userDataInContainer = it_users->second;

    const auto queryText =
        QString("INSERT INTO `Users` "
                "(`User`, `Password`, `Config`, `CreateUser`, `LastLogin`) "
                "VALUES "
                "('%1', '%2', '%3', '%4', '%5')")
            .arg(userDataInContainer.user())
            .arg(userDataInContainer.password())
            .arg(userDataInContainer.config().toJson())
            .arg(QDateTime::currentDateTime().toString(DATETIME_FORMAT))
            .arg(userDataInContainer.lastLogin().toString(DATETIME_FORMAT));

    try
    {
        DBQueryExecute(_db, queryText);
    }
    catch (const SQLException& err)
    {
        emit errorOccurred(EXIT_CODE::SQL_NOT_CONNECT, connectDBErrorString(_db));

        return userDataInContainer;
    }

    emit sendLogMsg(TDBLoger::MSG_CODE::INFORMATION_CODE, QString("Added user: %1").arg(user));

    return userDataInContainer;
}


