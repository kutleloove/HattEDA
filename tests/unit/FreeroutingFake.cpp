#include <QCoreApplication>
#include <QFile>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const int output = arguments.indexOf(QStringLiteral("-do"));
    if (output < 0 || output + 1 >= arguments.size()) return 2;
    QFile ses(arguments[output + 1]);
    if (!ses.open(QIODevice::WriteOnly)) return 3;
    ses.write("(session \"fake\" (routes))\n");
    return 0;
}
