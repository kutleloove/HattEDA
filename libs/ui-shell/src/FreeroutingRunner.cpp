#include "hatt/ui/FreeroutingRunner.hpp"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>

namespace hatt::ui {

FreeroutingRunner::FreeroutingRunner(QObject* parent) : QObject(parent) {}

FreeroutingRunner::~FreeroutingRunner() {
    if (process_ != nullptr && process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(1000);
    }
}

bool FreeroutingRunner::isRunning() const noexcept {
    return process_ != nullptr && process_->state() != QProcess::NotRunning;
}

void FreeroutingRunner::start(const FreeroutingRequest& request) {
    if (isRunning()) {
        complete({{}, {}, tr("An autorouter job is already running.")});
        return;
    }
    if (request.dsn.isEmpty()) {
        complete({{}, {}, tr("The autorouter input is empty.")});
        return;
    }
    if (request.jarPath.isEmpty() || !QFileInfo::exists(request.jarPath)) {
        complete({{}, {}, tr("Select a local Freerouting JAR file.")});
        return;
    }

    directory_ = std::make_unique<QTemporaryDir>();
    if (!directory_->isValid()) {
        complete({{}, {}, tr("A temporary autorouter directory could not be created.")});
        return;
    }
    const QString inputPath = directory_->filePath(QStringLiteral("board.dsn"));
    outputPath_ = directory_->filePath(QStringLiteral("board.ses"));
    QFile input(inputPath);
    if (!input.open(QIODevice::WriteOnly) || input.write(request.dsn) != request.dsn.size() ||
        !input.flush()) {
        complete({{}, {}, tr("The temporary autorouter input could not be written.")});
        return;
    }
    input.close();

    completing_ = false;
    cancelled_ = false;
    timedOut_ = false;
    process_ = new QProcess(this);
    process_->setWorkingDirectory(directory_->path());
    timeout_ = new QTimer(this);
    timeout_->setSingleShot(true);
    connect(timeout_, &QTimer::timeout, this, [this] {
        if (!isRunning()) return;
        timedOut_ = true;
        process_->kill();
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || completing_) return;
        FreeroutingResult result;
        result.error = tr("Freerouting could not be started: %1").arg(process_->errorString());
        complete(result);
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (completing_) return;
                FreeroutingResult result;
                result.cancelled = cancelled_;
                result.timedOut = timedOut_;
                result.diagnostics = QString::fromUtf8(process_->readAllStandardOutput()) +
                                     QString::fromUtf8(process_->readAllStandardError());
                if (result.cancelled) {
                    result.error = tr("Autorouting was cancelled.");
                } else if (result.timedOut) {
                    result.error = tr("Autorouting exceeded its time limit.");
                } else if (status != QProcess::NormalExit || exitCode != 0) {
                    result.error = tr("Freerouting failed with exit code %1.").arg(exitCode);
                } else {
                    QFile output(outputPath_);
                    if (!output.open(QIODevice::ReadOnly)) {
                        result.error = tr("Freerouting did not produce a session file.");
                    } else {
                        result.ses = output.readAll();
                        if (result.ses.isEmpty()) {
                            result.error = tr("Freerouting produced an empty session file.");
                        }
                    }
                }
                complete(result);
            });

    QStringList arguments{QStringLiteral("-jar"),
                          QFileInfo(request.jarPath).absoluteFilePath(),
                          QStringLiteral("-de"),
                          inputPath,
                          QStringLiteral("-do"),
                          outputPath_,
                          QStringLiteral("-mp"),
                          QString::number(std::max(1, request.maxPasses)),
                          QStringLiteral("--gui.enabled=false"),
                          QStringLiteral("--api_server.enabled=false"),
                          QStringLiteral("--logging.file.enabled=false"),
                          QStringLiteral("-da")};
    if (request.threadCount > 0)
        arguments << QStringLiteral("-mt") << QString::number(request.threadCount);
    const QSet<QString> updateStrategies{QStringLiteral("greedy"), QStringLiteral("global"),
                                         QStringLiteral("hybrid")};
    if (updateStrategies.contains(request.updateStrategy))
        arguments << QStringLiteral("-us") << request.updateStrategy;
    const QSet<QString> selectionStrategies{QStringLiteral("sequential"), QStringLiteral("random"),
                                            QStringLiteral("prioritized")};
    if (selectionStrategies.contains(request.selectionStrategy))
        arguments << QStringLiteral("-is") << request.selectionStrategy;
    process_->setProgram(request.javaExecutable);
    process_->setArguments(arguments);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->start();
    timeout_->start(std::max(1000, request.timeoutMs));
}

void FreeroutingRunner::cancel() {
    if (!isRunning()) return;
    cancelled_ = true;
    process_->kill();
}

void FreeroutingRunner::complete(FreeroutingResult result) {
    if (completing_) return;
    completing_ = true;
    if (timeout_ != nullptr) timeout_->stop();
    if (process_ != nullptr) {
        process_->deleteLater();
        process_ = nullptr;
    }
    if (timeout_ != nullptr) {
        timeout_->deleteLater();
        timeout_ = nullptr;
    }
    directory_.reset();
    outputPath_.clear();
    emit finished(result);
    completing_ = false;
}

} // namespace hatt::ui
