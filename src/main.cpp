#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("skachay"));
    QApplication::setOrganizationName(QStringLiteral("skachay"));
    QApplication::setApplicationDisplayName(QStringLiteral("Скачай"));
    QApplication::setApplicationVersion(QStringLiteral(SKACHAY_VERSION));
    QApplication::setQuitOnLastWindowClosed(false); // живём в трее

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Менеджер загрузок"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("url"), QStringLiteral("Ссылки для скачивания"));
    parser.process(app);

    MainWindow w;
    w.show();

    const QStringList urls = parser.positionalArguments();
    if (!urls.isEmpty())
        w.addUrls(urls, QString());

    return app.exec();
}
