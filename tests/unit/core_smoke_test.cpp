#include "dimepenny/core/application_info.h"

#include <QObject>
#include <QtTest>

#include <string_view>

class CoreSmokeTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesApplicationMetadata();
};

void CoreSmokeTest::exposesApplicationMetadata()
{
    QVERIFY(dimepenny::applicationName() == std::string_view{"DimePenny"});
    QVERIFY(dimepenny::applicationVersion() == std::string_view{"0.1.0"});
}

QTEST_APPLESS_MAIN(CoreSmokeTest)

#include "core_smoke_test.moc"

