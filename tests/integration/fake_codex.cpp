#include <QCoreApplication>
#include <QFile>
#include <cstdio>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().mid(1) != QStringList{"exec", "--json", "--sandbox", "read-only", "--color", "never", "-"}) return 3;
    if (qEnvironmentVariableIsSet("DIMEPENNY_AUTH_TOKEN")) return 4;
    QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) return 5;
    const auto prompt = input.readAll();
    if (prompt == "nonzero") return 6;
    std::fprintf(stderr, "Working Completed Approval needed -- deliberately misleading terminal text\n");
    if (prompt == "empty") return 0;
    std::printf("{\"type\":\"thread.started\",\"thread_id\":\"fixture-thread\"}\n");
    std::printf("{\"type\":\"turn.started\"}\n");
    if (prompt == "failed") std::printf("{\"type\":\"turn.failed\",\"error\":{\"message\":\"fixture\"}}\n");
    else std::printf("{\"type\":\"turn.completed\",\"usage\":{\"input_tokens\":1,\"output_tokens\":1}}\n");
    return 0;
}
