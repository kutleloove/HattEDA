#include "hatt/ui/FreeroutingRunner.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace hatt::ui;

class FreeroutingRunnerTests final : public QObject {
    Q_OBJECT

private slots:
    void rejectsMissingJar();
    void runsHeadlessProcessAndReturnsSession();
};

void FreeroutingRunnerTests::rejectsMissingJar() {
    FreeroutingRunner runner;
    QSignalSpy finished(&runner, &FreeroutingRunner::finished);
    FreeroutingRequest request;
    request.jarPath = QStringLiteral("missing.jar");
    request.dsn = "(pcb test)";
    runner.start(request);
    QCOMPARE(finished.size(), 1);
    const auto result = qvariant_cast<FreeroutingResult>(finished.first().first());
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.ses.isEmpty());
}

void FreeroutingRunnerTests::runsHeadlessProcessAndReturnsSession() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString jar = directory.filePath(QStringLiteral("fake.jar"));
    QFile placeholder(jar);
    QVERIFY(placeholder.open(QIODevice::WriteOnly));
    placeholder.write("fake");
    placeholder.close();

    FreeroutingRunner runner;
    QSignalSpy finished(&runner, &FreeroutingRunner::finished);
    FreeroutingRequest request;
    request.javaExecutable = QCoreApplication::applicationDirPath() +
                             QStringLiteral("/hatt-freerouting-fake.exe");
    request.jarPath = jar;
    request.dsn = "(pcb test)";
    request.timeoutMs = 5000;
    runner.start(request);
    QVERIFY(finished.wait(5000));
    const auto result = qvariant_cast<FreeroutingResult>(finished.first().first());
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.ses, QByteArray("(session \"fake\" (routes))\n"));
    QVERIFY(!runner.isRunning());
}

QTEST_MAIN(FreeroutingRunnerTests)
#include "FreeroutingRunnerTests.moc"
