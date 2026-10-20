#include "dimepenny/integration/codex_exec.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <utility>

namespace dimepenny {
namespace { constexpr qsizetype maxLine = 1024 * 1024; }
void CodexLifecycle::fail(const QString& reason) {
    if (failed_ || ended_) return;
    failed_ = true; buffer_.clear();
    emit lifecycle("SessionError"); emit rejected(reason);
}
void CodexLifecycle::consume(const QByteArray& bytes) {
    if (failed_ || ended_) return;
    // Consume incrementally, so even a huge batch cannot grow the partial-line buffer.
    for (const char c : bytes) {
        if (c == '\n') {
            const auto value = std::exchange(buffer_, {});
            if (!value.trimmed().isEmpty()) line(value);
            if (failed_) return;
        } else {
            if (buffer_.size() >= maxLine) { fail("Codex JSON event exceeds 1 MiB"); return; }
            buffer_.append(c);
        }
    }
}
void CodexLifecycle::line(const QByteArray& bytes) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail("Invalid Codex JSON event"); return;
    }
    const auto value = document.object();
    if (!value.value("type").isString()) { fail("Codex event has no type"); return; }
    const auto type = value.value("type").toString();
    if (type == "thread.started") {
        const auto id = value.value("thread_id").toString();
        if (id.isEmpty() || id.size() > 256 || (!thread_.isEmpty() && thread_ != id)) {
            fail("Invalid or changed Codex thread identity"); return;
        }
        if (thread_.isEmpty()) { thread_ = id; emit lifecycle("SessionConnected"); }
    } else if (type == "turn.started") {
        if (thread_.isEmpty() || completed_) { fail("Out-of-order Codex turn start"); return; }
        if (!started_) { started_ = true; emit lifecycle("TaskStarted"); }
    } else if (type == "turn.completed") {
        if (!started_) { fail("Codex completion without a turn start"); return; }
        if (!completed_) { completed_ = true; emit lifecycle("TaskCompleted"); }
    } else if (type == "turn.failed") {
        fail("Codex reported a failed turn");
    }
    // item.* and error diagnostics are not lifecycle guesses. In particular,
    // a retryable error is not a failed turn. Unknown additive events are ignored.
}
void CodexLifecycle::finish(bool successfulExit) {
    if (ended_) return;
    if (!failed_ && !buffer_.trimmed().isEmpty()) line(std::exchange(buffer_, {}));
    if (!successfulExit || !completed_) fail("Codex exited without a successful completed turn");
    ended_ = true;
}
CodexExec::CodexExec(QObject* parent) : QObject(parent) {
    connect(&decoder_, &CodexLifecycle::lifecycle, this, &CodexExec::lifecycle);
    connect(&decoder_, &CodexLifecycle::rejected, this, [this](const QString& reason) {
        emit diagnostic(reason);
        if (process_.state() != QProcess::NotRunning) process_.kill();
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, &CodexExec::read);
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        // Drain but do not interpret or persist stderr (may contain sensitive text).
        process_.readAllStandardError();
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        read(); complete(code == 0 && status == QProcess::NormalExit);
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false);
    });
}
CodexExec::~CodexExec() {
    process_.disconnect(this); decoder_.disconnect(this);
    if (process_.state() != QProcess::NotRunning) { process_.kill(); process_.waitForFinished(1000); }
}
void CodexExec::start(const QString& executable, const QString& directory, const QString& prompt) {
    if (used_) return;
    used_ = true;
    const QFileInfo binary(executable), project(directory);
    if (!binary.isAbsolute() || !binary.isFile() || !project.isAbsolute() || !project.isDir()
        || prompt.trimmed().isEmpty() || prompt.toUtf8().size() > maxLine
#ifdef Q_OS_WIN
        || binary.suffix().compare("exe", Qt::CaseInsensitive) != 0
#endif
        ) { complete(false); return; }
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("DIMEPENNY_AUTH_TOKEN");
    process_.setProcessEnvironment(environment);
    process_.setWorkingDirectory(project.absoluteFilePath());
    process_.setProgram(binary.absoluteFilePath());
    process_.setArguments({"exec", "--json", "--sandbox", "read-only", "--color", "never", "-"});
    connect(&process_, &QProcess::started, this, [this, prompt] {
        process_.write(prompt.toUtf8()); process_.closeWriteChannel();
    });
    process_.start();
}
void CodexExec::read() {
    process_.setReadChannel(QProcess::StandardOutput);
    while (process_.bytesAvailable() > 0) decoder_.consume(process_.read(64 * 1024));
}
void CodexExec::complete(bool success) {
    if (finished_) return;
    finished_ = true; decoder_.finish(success);
    emit finished(success && !decoder_.failed());
}
}
