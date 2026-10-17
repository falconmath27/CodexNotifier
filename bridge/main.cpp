#include "dimepenny/core/application_info.h"

#include <QCoreApplication>
#include <QDebug>
#include <QString>

namespace {

QString toQString(const std::string_view text)
{
    return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("DimePennyBridge");
    QCoreApplication::setApplicationVersion(toQString(dimepenny::applicationVersion()));

    qInfo().noquote()
        << "Dime&Penny bridge" << toQString(dimepenny::applicationVersion())
        << "project scaffold is ready.";

    return 0;
}

