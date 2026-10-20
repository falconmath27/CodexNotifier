#pragma once
#include <QObject>
#include <QProcess>

namespace dimepenny {
// Supported source: codex exec --json stdout, never terminal text or stderr.
// One decoder belongs to one process/run. No ambient thread discovery.
class CodexLifecycle final : public QObject {
    Q_OBJECT
public:
    explicit CodexLifecycle(QObject* parent = nullptr) : QObject(parent) {}
    void consume(const QByteArray& bytes);
    void finish(bool successfulExit);
    QString threadId() const { return thread_; }
    bool failed() const { return failed_; }
signals:
    void lifecycle(const QString& event);
    void rejected(const QString& reason);
private:
    QByteArray buffer_;
    QString thread_;
    bool started_ = false, completed_ = false, failed_ = false, ended_ = false;
    void line(const QByteArray& bytes);
    void fail(const QString& reason);
};

class CodexExec final : public QObject {
    Q_OBJECT
public:
    explicit CodexExec(QObject* parent = nullptr);
    ~CodexExec() override;
    // Explicitly launches a new read-only run using existing Codex CLI authentication.
    // Native executable only: no shell, command interpolation, or arbitrary extra flags.
    void start(const QString& executable, const QString& directory, const QString& prompt);
    QString threadId() const { return decoder_.threadId(); }
signals:
    void lifecycle(const QString& event);
    void diagnostic(const QString& reason);
    void finished(bool success);
private:
    QProcess process_;
    CodexLifecycle decoder_;
    bool used_ = false, finished_ = false;
    void read();
    void complete(bool success);
};
}
