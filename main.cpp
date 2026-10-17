#include "mainwindow.h"
#include <QApplication>
#include <QTranslator>
#include <QInputDialog>
#include <string>
#include <QSettings>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>
#include "src/api/api_server.h"
#include "src/api/services/gto_application_service.h"


void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if(MainWindow::s_textEdit == 0)
    {
        QByteArray localMsg = msg.toLocal8Bit();
        switch (type) {
        case QtDebugMsg:
            fprintf(stderr, "Debug: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
            break;
        case QtWarningMsg:
            fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
            break;
        case QtCriticalMsg:
            fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
            break;
        case QtFatalMsg:
            fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
            abort();
        }
    }
    else
    {
        // redundant check, could be removed, or the
        // upper if statement could be removed
        if(MainWindow::s_textEdit != 0){
            MainWindow::s_textEdit->log_with_signal(msg);
            MainWindow::s_textEdit->update();
        }
    }
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(myMessageOutput);
    QApplication a(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("TexasSolver GUI and local GTO API"));
    parser.addHelpOption();
    const QCommandLineOption apiEnabledOption(
        QStringLiteral("api-enabled"),
        QStringLiteral("Enable the local GTO WebSocket JSON-RPC API."));
    const QCommandLineOption apiPortOption(
        QStringLiteral("api-port"),
        QStringLiteral("Port for the local GTO API; 0 selects a random port."),
        QStringLiteral("port"),
        QStringLiteral("0"));
    const QCommandLineOption apiDiscoveryFileOption(
        QStringLiteral("api-discovery-file"),
        QStringLiteral("Path of the API discovery file."),
        QStringLiteral("path"));
    parser.addOption(apiEnabledOption);
    parser.addOption(apiPortOption);
    parser.addOption(apiDiscoveryFileOption);
    parser.process(a);

    bool portOk = false;
    const uint requestedPort = parser.value(apiPortOption).toUInt(&portOk);
    if (!portOk || requestedPort > 65535) {
        qCritical().noquote() << QStringLiteral("Invalid --api-port value: %1")
                                     .arg(parser.value(apiPortOption));
        return 1;
    }

    QSettings setting("TexasSolver", "Setting");
    setting.beginGroup("solver");
    QString language_str = setting.value("language").toString();
    QTranslator trans;

    if(language_str == ""){
        QStringList languages;
        languages << "English" << QString::fromLocal8Bit("简体中文");
        QString lang = QInputDialog::getItem(NULL,"select language","language",languages,0,false);

        if(lang == "English"){
            trans.load(":/lang_en.qm");
            language_str = "EN";
        }else if(lang == QString::fromLocal8Bit("简体中文")){
            trans.load(":/lang_cn.qm");
            language_str = "CN";
        }
        a.installTranslator(&trans);
        setting.setValue("language",language_str);
    }else{
        if(language_str == "EN"){
            trans.load(":/lang_en.qm");
        }else if(language_str == "CN"){
            trans.load(":/lang_cn.qm");
        }
        a.installTranslator(&trans);
    }

    int dump_round = setting.value("dump_round").toInt();
    if(dump_round == 0){
        setting.setValue("dump_round",2);
    }

    GtoApi::GtoApplicationService applicationService;
    MainWindow w(nullptr, &applicationService);
    w.show();

    GtoApi::LocalApiServer apiServer(&applicationService);
    if (parser.isSet(apiEnabledOption)) {
        GtoApi::ApiServerConfig config;
        config.port = static_cast<quint16>(requestedPort);
        config.discoveryFilePath = parser.value(apiDiscoveryFileOption);
        QString errorMessage;
        if (!apiServer.start(config, &errorMessage)) {
            qCritical().noquote() << QStringLiteral("Failed to start GTO API: %1")
                                         .arg(errorMessage);
            return 1;
        }
    }

    return a.exec();
}
