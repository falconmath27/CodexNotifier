#include "dimepenny/core/application_info.h"
#include "activation_window.h"

#include <QGuiApplication>
#include <QQuickStyle>
#include <QString>

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

    dimepenny::ActivationWindow window;
    if (window.status() != QQuickView::Ready) return 1;

    window.show();
    return application.exec();
}

