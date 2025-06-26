//Qt
#include <QHttpServerResponse>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSslKey>
#include <QSslServer>

//My
#include <Common/common.h>

#include <TradingCatCommon/transmitdata.h>
#include <TradingCatCommon/appserverprotocol.h>
#include <TradingCatCommon/transmitdata.h>

#include "appserver.h"

using namespace TradingCatCommon;
using namespace Common;

AppServer::AppServer(const TradingCatCommon::HTTPServerConfig& serverConfig,
                     const TradingCatCommon::TradingData& tradingData,
                     UsersCore& usersCore,
                     QObject* parent /* = nullptr */)
    : QObject{parent}
    , _serverConfig(serverConfig)
    , _tradingData(tradingData)
    , _usersCore(usersCore)
{
}

AppServer::~AppServer()
{
    stop();
}

void AppServer::start()
{
    Q_ASSERT(!_isStarted);

    if (!makeServer())
    {
        return;
    }

    _isStarted = true;
}

void AppServer::stop()
{
    if (!_isStarted)
    {
        emit finished();

        return;
    }
    _httpServer->disconnect();
    _httpServer.reset();

    _tcpServer.reset();

    _isStarted = false;

    emit finished();
}

QString AppServer::loginUser(const QHttpServerRequest &request)
{
    const auto query = request.query();

    LoginQuery queryData(query);

    emit sendLogMsg(MSG_CODE::INFORMATION_CODE, QString("%1 GET Request Login from %2:%3")
                                                              .arg(queryData.id())
                                                              .arg(request.remoteAddress().toString())
                                                              .arg(request.remotePort()));

    if (queryData.isError())
    {
        emit sendLogMsg(MSG_CODE::WARNING_CODE, QString("%1 Bad request. Error: %2 Source: %3")
                                                              .arg(queryData.id())
                                                              .arg(queryData.errorString())
                                                              .arg(request.url().toString()));

        return Package(StatusAnswer::ErrorCode::BAD_REQUEST, queryData.errorString()).toJson();
    }

    return _usersCore.login(queryData);
}

QString AppServer::logoutUser(const QHttpServerRequest &request)
{
    const auto query = request.query();

    LogoutQuery queryData(query);

    emit sendLogMsg(MSG_CODE::INFORMATION_CODE, QString("%1 GET Request Logout from %2:%3")
                                                              .arg(queryData.id())
                                                              .arg(request.remoteAddress().toString())
                                                              .arg(request.remotePort()));

    if (queryData.isError())
    {
        emit sendLogMsg(MSG_CODE::WARNING_CODE, QString("%1 Bad request. Error: %2 Source: %3")
                            .arg(queryData.id())
                            .arg(queryData.errorString())
                            .arg(request.url().toString()));

        return Package(StatusAnswer::ErrorCode::BAD_REQUEST, queryData.errorString()).toJson();
    }

    return _usersCore.logout(queryData);
}

QString AppServer::configUser(const QHttpServerRequest &request)
{
    const auto query = request.query();

    ConfigQuery queryData(query);

    emit sendLogMsg(MSG_CODE::INFORMATION_CODE, QString("%1 GET Request Config from %2:%3")
                                                              .arg(queryData.id())
                                                              .arg(request.remoteAddress().toString())
                                                              .arg(request.remotePort()));

    if (queryData.isError())
    {
        emit sendLogMsg(MSG_CODE::WARNING_CODE, QString("%1 Bad request. Error: %2 Source: %3")
                            .arg(queryData.id())
                            .arg(queryData.errorString())
                            .arg(request.url().toString()));

        return Package(StatusAnswer::ErrorCode::BAD_REQUEST, queryData.errorString()).toJson();
    }

    return _usersCore.config(queryData);
}

QString AppServer::detectData(const QHttpServerRequest &request)
{
    const auto query = request.query();

    DetectQuery queryData(query);

    emit sendLogMsg(MSG_CODE::INFORMATION_CODE, QString("%1 GET Request Detect from %2:%3")
                                                              .arg(queryData.id())
                                                              .arg(request.remoteAddress().toString())
                                                              .arg(request.remotePort()));

    if (queryData.isError())
    {
        emit sendLogMsg(MSG_CODE::WARNING_CODE, QString("%1 Bad request. Error: %2 Source: %3")
                            .arg(queryData.id())
                            .arg(queryData.errorString())
                            .arg(request.url().toString()));

        return Package(StatusAnswer::ErrorCode::BAD_REQUEST, queryData.errorString()).toJson();
    }

    return _usersCore.detect(queryData);
}

QString AppServer::stockExchangesData(const QHttpServerRequest &request)
{
    const auto query = request.query();

    StockExchangesQuery queryData(query);

    emit sendLogMsg(MSG_CODE::INFORMATION_CODE, QString("%1 GET Request StockExchanges from %2:%3")
                                                              .arg(queryData.id())
                                                              .arg(request.remoteAddress().toString())
                                                              .arg(request.remotePort()));

    if (queryData.isError())
    {
        emit sendLogMsg(MSG_CODE::WARNING_CODE, QString("%1 Bad request. Error: %2 Source: %3")
                            .arg(queryData.id())
                            .arg(queryData.errorString())
                            .arg(request.url().toString()));

        return Package(StatusAnswer::ErrorCode::BAD_REQUEST, queryData.errorString()).toJson();
    }

    return _usersCore.stockExchange(queryData);
}

QString AppServer::klinesIdList(const QHttpServerRequest &request)
{
    const auto query = request.query();

    KLinesIDListQuery queryData(query);

    emit sendLogMsg(MSG_CODE::INFORMATION_CODE, QString("%1 GET Request KLinesIDList from %2:%3")
                                                              .arg(queryData.id())
                                                              .arg(request.remoteAddress().toString())
                                                              .arg(request.remotePort()));

    if (queryData.isError())
    {
        emit sendLogMsg(MSG_CODE::WARNING_CODE, QString("%1 Bad request. Error: %2 Source: %3")
                            .arg(queryData.id())
                            .arg(queryData.errorString())
                            .arg(request.url().toString()));

        return Package(StatusAnswer::ErrorCode::BAD_REQUEST, queryData.errorString()).toJson();
    }

    return _usersCore.klinesIdList(queryData);
}

QString AppServer::serverStatus(const QHttpServerRequest &request)
{
    ServerStatusQuery queryData(request.query());

    emit sendLogMsg(MSG_CODE::INFORMATION_CODE, QString("%1 GET Request ServerStatus from %2:%3")
                        .arg(queryData.id())
                        .arg(request.remoteAddress().toString())
                        .arg(request.remotePort()));

    const auto currDateTime = QDateTime::currentDateTime();
    const auto appName = QString("%1 (Total money: %2)")
                             .arg(_serverConfig.name.isEmpty() ? QCoreApplication::applicationName() : _serverConfig.name)
                             .arg(_tradingData.moneyCount());

    ServerStatusAnswer statusJson(appName, QCoreApplication::applicationVersion(), currDateTime, _startDateTime.secsTo(currDateTime), _usersCore.usersOnline());

    emit sendLogMsg(MSG_CODE::INFORMATION_CODE, QString("%1 Successfully finished. Send answer").arg(queryData.id()));

    return Package(statusJson).toJson();
}

bool AppServer::makeServer()
{
    try
    {
        _httpServer = std::make_unique<QHttpServer>();

        _httpServer->route(LoginQuery().path(), QHttpServerRequest::Method::Get,
                           [this](const QHttpServerRequest &request)
                           {
                               return loginUser(request);
                           });

        _httpServer->route(LoginQuery().path(), QHttpServerRequest::Method::Options,
                           []()
                           {
                               return QString();
                           });

        _httpServer->route(LogoutQuery().path(), QHttpServerRequest::Method::Get,
                           [this](const QHttpServerRequest &request)
                           {
                               return logoutUser(request);
                           });

        _httpServer->route(LogoutQuery().path(), QHttpServerRequest::Method::Options,
                           []()
                           {
                               return QString();
                           });

        _httpServer->route(ConfigQuery().path(), QHttpServerRequest::Method::Get,
                           [this](const QHttpServerRequest &request)
                           {
                               return configUser(request);
                           });

        _httpServer->route(ConfigQuery().path(), QHttpServerRequest::Method::Options,
                           []()
                           {
                               return QString();
                           });

        _httpServer->route(DetectQuery().path(), QHttpServerRequest::Method::Get,
                           [this](const QHttpServerRequest &request)
                           {
                               return detectData(request);
                           });

        _httpServer->route(DetectQuery().path(), QHttpServerRequest::Method::Options,
                           []()
                           {
                               return QString();
                           });

        _httpServer->route(StockExchangesQuery().path(), QHttpServerRequest::Method::Get,
                           [this](const QHttpServerRequest &request)
                           {   
                               return stockExchangesData(request);
                           });

        _httpServer->route(StockExchangesQuery().path(), QHttpServerRequest::Method::Options,
                           []()
                           {
                               return QString();
                           });

        _httpServer->route(KLinesIDListQuery().path(), QHttpServerRequest::Method::Get,
                           [this](const QHttpServerRequest &request)
                           {
                               return klinesIdList(request);
                           });

        _httpServer->route(KLinesIDListQuery().path(), QHttpServerRequest::Method::Options,
                           []()
                           {
                               return QString();
                           });

        _httpServer->route(ServerStatusQuery().path(), QHttpServerRequest::Method::Get,
                           [this](const QHttpServerRequest &request)
                           {
                               return serverStatus(request);
                           });

        _httpServer->route(ServerStatusQuery().path(), QHttpServerRequest::Method::Options,
                           []()
                           {
                               return QString();
                           });

        _httpServer->setMissingHandler(_httpServer.get(),
            [this](const QHttpServerRequest& req, QHttpServerResponder& resp)
            {
                Q_UNUSED(resp);

                emit sendLogMsg(MSG_CODE::WARNING_CODE, QString("Unknow %1 request from %2:%3 %4")
                                     .arg(static_cast<quint16>(req.method()))
                                    .arg(req.remoteAddress().toString())
                                    .arg(req.remotePort())
                                    .arg(req.url().toString()));

                QHttpServerResponse response(Package(StatusAnswer::ErrorCode::NOT_FOUND).toJson(), QHttpServerResponder::StatusCode::NotFound);

                resp.sendResponse(response);
            });

        _httpServer->addAfterRequestHandler(_httpServer.get(),
            [this](const QHttpServerRequest& req, QHttpServerResponse& resp)
            {
                Q_UNUSED(req);

                auto h = resp.headers();
                h.append(QHttpHeaders::WellKnownHeader::Server, _serverConfig.name);
                h.append(QHttpHeaders::WellKnownHeader::ContentType, "application/json");
#ifdef QT_DEBUG
                h.append(QHttpHeaders::WellKnownHeader::ContentLength, QString::number(resp.data().size()));
                h.append(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin, "*");
                h.append(QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders, "*");
                h.append(QHttpHeaders::WellKnownHeader::AccessControlAllowMethods, "*");
#endif

                resp.setHeaders(std::move(h));
            });

        _tcpServer = std::make_unique<QTcpServer>();
        if (!_tcpServer->listen(_serverConfig.address, _serverConfig.port) ||
           (!_httpServer->bind(_tcpServer.get())))
        {
            throw Common::StartException(EXIT_CODE::HTTP_SERVER_NOT_LISTEN, QString("Cannot start HTTP Server on %1:%2. Error: %3")
                                                                                .arg(_tcpServer->serverAddress().toString())
                                                                                .arg(_tcpServer->serverPort())
                                                                                .arg(_tcpServer->errorString()));
        }

    }
    catch (const Common::StartException& err)
    {
        _httpServer.reset();
        _tcpServer.reset();

        emit errorOccurred(EXIT_CODE::HTTP_SERVER_NOT_LISTEN, err.what());

        return false;
    }

    emit sendLogMsg(MSG_CODE::INFORMATION_CODE, QString("HTTP server has listening on %1:%2").arg(_tcpServer->serverAddress().toString()).arg(_tcpServer->serverPort()));

    return true;
}


