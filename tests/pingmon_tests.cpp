#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "theme.h"
#include "translator.h"

class PingmonTests : public QObject {
    Q_OBJECT

private slots:
    void parsesServerStats()
    {
        const QByteArray json = R"({
          "8.8.8.8": {"status": "up", "consecutive_failures": 0, "avg_latency": 12.5, "min_latency": 10.0,
                      "max_latency": 15.0, "packet_loss_pct": 33.3, "total_sent": 3,
                      "history": [10.0, null, 15.0], "resolved_ip": null},
          "example.com": {"status": "down", "consecutive_failures": 4, "avg_latency": null, "min_latency": null,
                          "max_latency": null, "packet_loss_pct": 100, "total_sent": 4,
                          "history": [null, null, null, null], "resolved_ip": "93.184.216.34"}
        })";
        bool ok = false;
        const auto hosts = Backend::parseStats(json, &ok);
        QVERIFY(ok);
        QCOMPARE(hosts.size(), 2);

        const HostStats &down = hosts.at(0);
        QCOMPARE(down.name, QString("8.8.8.8") < QString("example.com") ? QString("8.8.8.8") : QString("example.com"));
        for (const auto &h : hosts) {
            if (h.name == "8.8.8.8") {
                QCOMPARE(h.status, QString("up"));
                QCOMPARE(h.average, 12.5);
                QCOMPARE(h.last, 15.0);
                QCOMPARE(h.history.at(1).toDouble(), -1.0);
            } else {
                QCOMPARE(h.status, QString("down"));
                QCOMPARE(h.average, -1.0);
                QCOMPARE(h.last, -1.0);
                QCOMPARE(h.resolvedIp, QString("93.184.216.34"));
                QCOMPARE(h.failures, 4);
            }
        }
    }

    void rejectsGarbageStats()
    {
        bool ok = true;
        QVERIFY(Backend::parseStats("not json", &ok).isEmpty());
        QVERIFY(!ok);
    }

    void startsWithDefaultHosts()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), dir.path() + "/server", dir.path() + "/none.json");
        QCOMPARE(backend.hosts()->count(), 2);
        QCOMPARE(backend.port(), 8765);
    }

    void readsPortAndThresholdsFromMonitorConfig()
    {
        QTemporaryDir dir;
        write(dir.path() + "/config.json", R"({"port": 9100, "latency_good_ms": 20, "latency_warn_ms": 80})");
        Backend backend(dir.path(), dir.path() + "/server", dir.path() + "/config.json");
        QCOMPARE(backend.port(), 9100);
        QCOMPARE(backend.goodMs(), 20);
        QCOMPARE(backend.warnMs(), 80);
    }

    void validatesHostNames()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), dir.path() + "/server", dir.path() + "/none.json");
        QCOMPARE(backend.addHost("  "), QString("empty"));
        QCOMPARE(backend.addHost("bad host/../x"), QString("invalid"));
        QCOMPARE(backend.addHost("-c5"), QString("invalid"));
        QCOMPARE(backend.addHost("1.1.1.1"), QString("duplicate"));
    }

    void savesHostsAtomically()
    {
        QTemporaryDir dir;
        {
            // No server script exists, so nothing is launched, but the list is kept.
            Backend backend(dir.path(), dir.path() + "/server", dir.path() + "/none.json");
            QCOMPARE(backend.addHost("router.local"), QString());
            backend.removeHost("1.1.1.1");
        }
        const QStringList hosts = Backend::loadHosts(dir.path() + "/hosts.json");
        QCOMPARE(hosts, (QStringList{"8.8.8.8", "router.local"}));
    }

    void missingServerIsReported()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), dir.path() + "/server", dir.path() + "/none.json");
        backend.toggleServer();
        QCOMPARE(backend.serverState(), QString("error"));
        QCOMPARE(backend.errorKind(), QString("script"));
    }

    void runsTheBundledServer()
    {
        QTemporaryDir dir;
        write(dir.path() + "/hosts.json", R"({"hosts": ["127.0.0.1"]})");
        write(dir.path() + "/config.json", R"({"port": 18765})");
        Backend backend(dir.path(), QString(SOURCE_DIR) + "/server", dir.path() + "/config.json");
        backend.start();
        QTRY_COMPARE_WITH_TIMEOUT(backend.serverState(), QString("running"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(backend.hosts()->count() == 1 && backend.hosts()->data(backend.hosts()->index(0), HostModel::SentRole).toInt() > 0, 10000);
        QVERIFY(backend.reachable());
        backend.stopServer();
        QTRY_COMPARE_WITH_TIMEOUT(backend.serverState(), QString("idle"), 5000);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(PingmonTests)
#include "pingmon_tests.moc"
