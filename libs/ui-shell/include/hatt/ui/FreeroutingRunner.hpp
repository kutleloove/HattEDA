#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

class QProcess;
class QTemporaryDir;
class QTimer;

namespace hatt::ui {

struct FreeroutingRequest {
#ifdef Q_OS_WIN
    QString javaExecutable = QStringLiteral("javaw");
#else
    QString javaExecutable = QStringLiteral("java");
#endif
    QString jarPath;
    QByteArray dsn;
    int maxPasses = 100;
    int threadCount = 0; // 0 = Freerouting automatic default
    QString updateStrategy = QStringLiteral("greedy");
    QString selectionStrategy = QStringLiteral("prioritized");
    int timeoutMs = 5 * 60 * 1000;
};

struct FreeroutingResult {
    QByteArray ses;
    QString diagnostics;
    QString error;
    bool cancelled = false;
    bool timedOut = false;
};

// One local, headless Freerouting process. The temporary DSN/SES directory lives only for the job;
// callers receive bytes and must validate/import them before changing the project.
class FreeroutingRunner final : public QObject {
    Q_OBJECT

public:
    explicit FreeroutingRunner(QObject* parent = nullptr);
    ~FreeroutingRunner() override;

    [[nodiscard]] bool isRunning() const noexcept;
    void start(const FreeroutingRequest& request);
    void cancel();

signals:
    void finished(const hatt::ui::FreeroutingResult& result);

private:
    void complete(FreeroutingResult result);

    std::unique_ptr<QTemporaryDir> directory_;
    QProcess* process_ = nullptr;
    QTimer* timeout_ = nullptr;
    QString outputPath_;
    bool completing_ = false;
    bool cancelled_ = false;
    bool timedOut_ = false;
};

} // namespace hatt::ui

Q_DECLARE_METATYPE(hatt::ui::FreeroutingResult)
