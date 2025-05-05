#pragma once

//STL
#include <unordered_map>

//Qt
#include <QObject>
#include <QDateTime>
#include <QSqlDatabase>
#include <QTimer>

//My
#include <Common/sql.h>
#include <Common/tdbloger.h>

#include <TradingCatCommon/userconfig.h>

///////////////////////////////////////////////////////////////////////////////
///     The UserData class - данные пользователя
///
class UserData final
{
public:
    UserData(const UserData& userData) = default;
    UserData& operator=(const UserData& userData) = default;
    UserData(UserData&& userData) = default;
    UserData& operator=(UserData&& userData) = default;
    UserData(const QString& user, const QString password, const QString& configJson, const QDateTime& lastLogin);

    const QString& user() const noexcept;
    const QString& password() const noexcept;

    const TradingCatCommon::UserConfig& config() const noexcept;
    void setConfig(const TradingCatCommon::UserConfig& config);

    const QDateTime& lastLogin() const noexcept;
    void setLastLogin(const QDateTime& lastLogin);

    bool isChange() const noexcept;
    void clearIsChange() noexcept;

    bool isError() const noexcept;
    const QString& errorString() const noexcept;

private:
    UserData() = delete;
    
private:
    QString _errorString;

    QString _user = "undefined";
    QString _password;
    TradingCatCommon::UserConfig _config;
    QDateTime _lastLogin = QDateTime::currentDateTime();

    bool _isChange = false;

};

///////////////////////////////////////////////////////////////////////////////
///     The Users class - класс-контейнер работы с данными пользователей
///
class Users final
    : public QObject
{
    Q_OBJECT

public:
    explicit Users(const Common::DBConnectionInfo& dbConnectionInfo, QObject* parent = nullptr);

    ~Users() override = default;

    UserData& user(const QString& user);
    bool isUserExist(const QString& user) const;

    UserData& newUser(const QString &user, const QString &password);

    void start();
    void stop();

signals:
    /*!
        Сообщение логеру
        @param category - категория сообщения
        @param msg - текст сообщения
    */
    void sendLogMsg(Common::TDBLoger::MSG_CODE category, const QString& msg);

    /*!
        Сигнал генерируется если в процессе работы сервера произошла фатальная ошибка
        @param errorCode - код ошибки
        @param errorString - текстовое описание ошибки
    */
    void errorOccurred(Common::EXIT_CODE errorCode, const QString& errorString);

private slots:
    void saveUserDataTimerTimeout();

private:
    void loadUserData();
    void saveUserData(const UserData &userData);

private:
    const Common::DBConnectionInfo _dbConnectionInfo;
    QSqlDatabase _db;
    
    std::unordered_map<QString, UserData> _users;

    QTimer* _saveUserDataTimer = nullptr;

    bool _isStarted = false;
};
