#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2 || QDir(app.arguments().at(1)).canonicalPath() != QDir::current().canonicalPath()) return 2;
    if (qEnvironmentVariableIsEmpty("DIMEPENNY_HOOK_TOKEN") || qEnvironmentVariableIsEmpty("DIMEPENNY_HOOK_ENDPOINT")) return 3;
    std::cout << "FAKE_CONSOLE_READY" << std::endl;
    std::string line;
    if (!std::getline(std::cin, line) || line != "interactive input") return 4;
    for (const auto* event : {"SessionStart", "UserPromptSubmit", "Stop", "SessionEnd"}) {
        QProcess helper;
        helper.start(QCoreApplication::applicationDirPath() + "/../bridge/DimePennyInteractive.exe", {"--hook"});
        if (!helper.waitForStarted()) return 5;
        helper.write(QJsonDocument(QJsonObject{{"hook_event_name", event}, {"session_id", "powershell-fixture"},
            {"cwd", QDir::currentPath()}, {"turn_id", "fixture-turn"}}).toJson());
        helper.closeWriteChannel();
        if (!helper.waitForFinished(5000) || helper.exitCode() != 0 || !helper.readAllStandardError().isEmpty()) return 6;
    }
    std::cout << "FAKE_CONSOLE_FINISHED" << std::endl;
    return 23;
}
