#include "leakcheck.h"

#include <QJsonDocument>
#include <QTest>

using namespace agv;

namespace {

QJsonArray answer(const char *json)
{
    return QJsonDocument::fromJson(json).array();
}

} // namespace

class TestLeakCheck : public QObject
{
    Q_OBJECT

private slots:
    void providerDnsLeaks()
    {
        // What a NetworkManager setup without systemd-resolved showed: the VPN exit in Greece,
        // DNS answered by the internet provider at home.
        const QVariantMap result = LeakCheck::evaluate(answer(R"([
            {"ip":"2a02:6ea0:4502::1","country":"gr","country_name":"Greece","asn":"AS212238 DataCamp Limited","org":"DataCamp Limited","type":"ip"},
            {"ip":"176.208.103.109","country":"ru","country_name":"Russia","asn":"AS12389 PJSC Rostelecom","org":"PJSC Rostelecom","type":"dns"},
            {"ip":"DNS may be leaking.","type":"conclusion"}])"));
        QCOMPARE(result.value(QStringLiteral("verdict")).toString(), QStringLiteral("leak"));
        QCOMPARE(result.value(QStringLiteral("country")).toString(), QStringLiteral("Greece"));
        QCOMPARE(result.value(QStringLiteral("dns")).toList().size(), 1);
    }

    void dnsInsideTheVpn()
    {
        const QVariantMap adguard = LeakCheck::evaluate(answer(R"([
            {"ip":"185.1.2.3","country":"de","country_name":"Germany","asn":"AS212238 DataCamp Limited","org":"DataCamp Limited","type":"ip"},
            {"ip":"94.140.14.14","country":"cy","country_name":"Cyprus","asn":"AS212772 AdGuard Software Limited","org":"AdGuard Software Limited","type":"dns"}])"));
        QCOMPARE(adguard.value(QStringLiteral("verdict")).toString(), QStringLiteral("ok"));

        const QVariantMap sameCountry = LeakCheck::evaluate(answer(R"([
            {"ip":"185.1.2.3","country":"de","country_name":"Germany","asn":"AS1 A","org":"A","type":"ip"},
            {"ip":"185.9.9.9","country":"de","country_name":"Germany","asn":"AS2 B","org":"B","type":"dns"}])"));
        QCOMPARE(sameCountry.value(QStringLiteral("verdict")).toString(), QStringLiteral("ok"));
    }

    void nothingSeen()
    {
        QCOMPARE(LeakCheck::evaluate({}).value(QStringLiteral("verdict")).toString(), QStringLiteral("error"));
    }
};

QTEST_GUILESS_MAIN(TestLeakCheck)
#include "tst_leakcheck.moc"
