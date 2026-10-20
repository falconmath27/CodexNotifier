#include "dimepenny/core/application_info.h"
#include "activation_window.h"

#include <QGuiApplication>
#include <QQuickStyle>
#include <QString>
#include <QDir>
#include <QStandardPaths>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace {

QString toQString(const std::string_view text)
{
    return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
}

}  // namespace

int main(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName("DimePenny");
    QGuiApplication::setApplicationDisplayName("Dime&Penny");
    QGuiApplication::setApplicationVersion(toQString(dimepenny::applicationVersion()));
    QGuiApplication::setOrganizationName("DimePenny");
    QQuickStyle::setStyle("FluentWinUI3");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"codex-executable", "Opt in to a real read-only run using this native Codex executable.", "path"});
    parser.addOption({"project", "Project directory for the real run.", "directory"});
    parser.addOption({"prompt-file", "UTF-8 prompt file for the real run (maximum 1 MiB).", "path"});
    parser.addOption({"companion", "dime or penny for the real run.", "type", "penny"});
    parser.process(application);
    QString prompt;
    if (parser.isSet("codex-executable")) {
        QFile file(parser.value("prompt-file"));
        if (!parser.isSet("project") || !parser.isSet("prompt-file") || !file.open(QIODevice::ReadOnly)
            || file.size() > 1024 * 1024 || !QStringList{"dime", "penny"}.contains(parser.value("companion"))) return 2;
        prompt = QString::fromUtf8(file.readAll());
        if (prompt.trimmed().isEmpty()) return 2;
    } else if (parser.isSet("project") || parser.isSet("prompt-file") || parser.isSet("companion")) return 2;

    const auto dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDirectory.isEmpty() || !QDir().mkpath(dataDirectory)) {
        qCritical("Cannot create application data directory");
        return 1;
    }
    dimepenny::ActivationWindow window(QDir(dataDirectory).filePath("dimepenny.sqlite3"));
    if (window.status() != QQuickView::Ready) return 1;
    if (!window.enableInteractive()) qWarning("Interactive launcher endpoint unavailable; see connection status");

    window.show();
    if (parser.isSet("codex-executable")) {
        QTimer::singleShot(0, &window, [&] {
            window.configure(QFileInfo(parser.value("project")).absoluteFilePath(), "PowerShell", parser.value("companion") == "dime" ? 0 : 1);
            window.activateCodex(QFileInfo(parser.value("codex-executable")).absoluteFilePath(), prompt);
        });
    }
    return application.exec();
}

