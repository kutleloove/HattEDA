#include "hatt/ui/MainWindow.hpp"
#include "hatt/ui/Theme.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QLocale>
#include <QTranslator>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("HattEDA"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("HattEDA"));

    QTranslator translator;
    const QString language = QSettings().value(QStringLiteral("appearance/language"),
                                                QLocale::system().name().left(2)).toString();
    if (language == QStringLiteral("tr") && translator.load(QStringLiteral(":/i18n/hatteda_tr.qm"))) {
        application.installTranslator(&translator);
    }

    const bool lightTheme = QSettings().value(QStringLiteral("appearance/lightTheme"), false).toBool();
    hatt::ui::Theme::apply(application,
                          lightTheme ? hatt::ui::ThemeMode::Light : hatt::ui::ThemeMode::Dark);

    hatt::ui::MainWindow window;
    window.show();
    return application.exec();
}
