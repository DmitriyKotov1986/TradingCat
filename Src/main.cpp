//Qt
#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>
#include <QTimer>
#include <QCommandLineParser>
#include <QFileInfo>

//My
#include <Common/common.h>
#include <Common/tdbloger.h>

#include "config.h"
#include "core.h"

using namespace Common;

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, ""); //настраиваем локаль
    qInstallMessageHandler(messageOutput);

    QCoreApplication a(argc, argv);

    QFileInfo exeFileInfo(a.applicationFilePath());

    QCoreApplication::setApplicationName(exeFileInfo.baseName());
    QCoreApplication::setOrganizationName("Cat software development");
    QCoreApplication::setApplicationVersion(QString("Version:0.2 Build: %1 %2").arg(__DATE__).arg(__TIME__));

    //Создаем парсер параметров командной строки
    QCommandLineParser parser;
    parser.setApplicationDescription("Getter data from stock exchange");
    parser.addHelpOption();
    parser.addVersionOption();

    //добавляем опцию Config
    QCommandLineOption config(QStringList() << "c" << "config", "Config file name", "FileName", QString("%1/%2.ini").arg(a.applicationDirPath()).arg(a.applicationName()));
    parser.addOption(config);

    //добавляем опцию MakeConfig
    QCommandLineOption makeConfig(QStringList() << "m" << "makeconfig", "Create new config file");
    parser.addOption(makeConfig);

    //Парсим опции командной строки
    parser.process(a);
    const auto configFileName = QFileInfo(parser.value(config)).absoluteFilePath();

    if (parser.isSet(makeConfig))
    {
        Config::makeConfig(configFileName);

        return EXIT_CODE::OK;
    }

    Config* cnf = nullptr;
    TDBLoger* loger = nullptr;
    Core* core = nullptr;
    try
    {
        cnf = Config::config(configFileName);
        if (cnf->isError())
        {
            throw StartException(EXIT_CODE::LOAD_CONFIG_ERR, QString("Error load configuration: %1").arg(cnf->errorString()));
        }

        Common::DEBUG_MODE = cnf->debugMode();

        //настраиваем подключение БД логирования
        loger = Common::TDBLoger::DBLoger(cnf->dbConnectionInfo(),
                                          cnf->logTableName().isEmpty() ? QString("%1Log").arg(a.applicationName()) : cnf->logTableName(),
                                          cnf->debugMode(),
                                          cnf->httpServerConfig().name.isEmpty() ? a.applicationName() : cnf->httpServerConfig().name);

        //создаем и запускаем сервис
        core = new Core();
        if (core->isError())
        {
            throw StartException(EXIT_CODE::SERVICE_INIT_ERR, QString("Core initialization error: %1").arg(core->errorString()));
        }

        loger->start();
        if (loger->isError())
        {
            throw StartException(EXIT_CODE::START_LOGGER_ERR, QString("Loger initialization error. Error: %1").arg(loger->errorString()));
        }
    }

    catch (const StartException& err)
    {
        delete core;
        TDBLoger::deleteDBLoger();
        Config::deleteConfig();

        qCritical() << err.what();

        return err.exitCode();
    }

    //Таймер запуска
    QTimer startTimer;
    startTimer.setSingleShot(true);

    QObject::connect(&startTimer, SIGNAL(timeout()), core, SLOT(start()));
    QObject::connect(core, SIGNAL(finished()), &a, SLOT(quit()));

    startTimer.start(0);

    const auto res = a.exec();

    core->stop();

    delete core;
    TDBLoger::deleteDBLoger();
    Config::deleteConfig();

    return res;
}


