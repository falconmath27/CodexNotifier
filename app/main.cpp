#include "dimepenny/core/application_info.h"

#include <QApplication>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString toQString(const std::string_view text)
{
    return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("DimePenny");
    QApplication::setApplicationDisplayName("Dime&Penny");
    QApplication::setApplicationVersion(toQString(dimepenny::applicationVersion()));
    QApplication::setOrganizationName("DimePenny");

    QWidget window;
    window.setWindowTitle("Dime&Penny");
    window.resize(480, 220);

    auto* layout = new QVBoxLayout(&window);
    auto* title = new QLabel("Dime&Penny", &window);
    auto* message = new QLabel(
        "Qt project scaffold is running.\n"
        "Session registration and desktop pets arrive in later tasks.",
        &window
    );

    title->setAlignment(Qt::AlignCenter);
    message->setAlignment(Qt::AlignCenter);
    message->setWordWrap(true);

    layout->addStretch();
    layout->addWidget(title);
    layout->addWidget(message);
    layout->addStretch();

    window.show();
    return application.exec();
}

