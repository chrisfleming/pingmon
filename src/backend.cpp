#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {
const QStringList kDefaultHosts{"1.1.1.1", "8.8.8.8"};

QString findServerDirectory() {
    const QString fromEnvironment = qEnvironmentVariable("PINGMON_SERVER_DIR");
    if (!fromEnvironment.isEmpty())
        return fromEnvironment;
    const QString installed = "/usr/share/pingmon/server";
    if (QFileInfo::exists(installed + "/ping_server.py"))
        return installed;
    // Running from a build directory inside the source tree.
    return QCoreApplication::applicationDirPath() + "/../server";
}
}

Backend::Backend(const QString &configDirectory, const QString &serverDirectory, const QString &monitorConfig,
                 QObject *parent)
    : QObject(parent),
      m_configDirectory(configDirectory.isEmpty() ? QDir::homePath() + "/.config/pingmon" : configDirectory),
      m_serverDirectory(serverDirectory.isEmpty() ? findServerDirectory() : serverDirectory) {
    // The server reads this same file for its port, so reading it here keeps
    // the two in agreement, and the thresholds match the CLI dashboard colors.
    QFile config(monitorConfig.isEmpty() ? QDir::homePath() + "/.config/ping-monitor/config.json" : monitorConfig);
    if (config.open(QIODevice::ReadOnly)) {
        const QJsonObject object = QJsonDocument::fromJson(config.readAll()).object();
        m_port = object.value("port").toInt(m_port);
        m_good = object.value("latency_good_ms").toInt(m_good);
        m_warn = object.value("latency_warn_ms").toInt(m_warn);
    }

    m_saved = loadHosts(m_configDirectory + "/hosts.json");
    showSavedHosts();

    // Python buffers stdout when piped, but a crash message goes to stderr
    // and that is what we show.
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    m_process.setProcessEnvironment(environment);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_log = (m_log + QString::fromUtf8(m_process.readAllStandardError())).right(2000);
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { m_process.readAllStandardOutput(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            setState("error", "python");
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (m_stopping) {
            m_stopping = false;
            showSavedHosts();
            setState("idle");
            return;
        }
        const QStringList lines = m_log.trimmed().split('\n', Qt::SkipEmptyParts);
        showSavedHosts();
        setState("error", "exited", lines.isEmpty() ? QString::number(code) : lines.last().trimmed());
    });

    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &Backend::poll);
    m_timer.start();
}

Backend::~Backend() {
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(2000)) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }
}

QUrl Backend::url(const QString &path) const {
    return QUrl(QString("http://127.0.0.1:%1%2").arg(m_port).arg(path));
}

QStringList Backend::loadHosts(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return kDefaultHosts;
    QStringList hosts;
    for (const auto &value : QJsonDocument::fromJson(file.readAll()).object().value("hosts").toArray())
        if (!value.toString().isEmpty())
            hosts << value.toString();
    return hosts;
}

void Backend::saveHosts() const {
    QDir().mkpath(m_configDirectory);
    // Written beside the target and renamed into place, so a crash never
    // leaves half a file.
    QSaveFile file(m_configDirectory + "/hosts.json");
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{"hosts", QJsonArray::fromStringList(m_saved)}}).toJson());
    file.commit();
}

// Without a live server the list still shows what is configured, so the
// window is never an empty mystery.
void Backend::showSavedHosts() {
    QList<HostStats> placeholders;
    for (const auto &name : m_saved) {
        HostStats stats;
        stats.name = name;
        placeholders << stats;
    }
    m_model.setHosts(placeholders);
}

void Backend::setState(const QString &state, const QString &kind, const QString &detail) {
    m_state = state;
    m_errorKind = kind;
    m_errorDetail = detail;
    if (state != "running" && state != "external")
        m_reachable = false;
    emit serverChanged();
}

void Backend::start() {
    setState("starting");
    QNetworkRequest request(url("/health"));
    request.setTransferTimeout(1000);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const bool answered = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (answered) {
            m_failures = 0;
            m_reachable = true;
            setState("external");
            poll();
        } else {
            launchServer();
        }
    });
}

void Backend::launchServer() {
    if (m_process.state() != QProcess::NotRunning)
        return;
    // The server needs at least one host to start with.
    if (m_saved.isEmpty()) {
        setState("idle");
        return;
    }
    const QString python = QStandardPaths::findExecutable("python3");
    if (python.isEmpty())
        return setState("error", "python");
    if (QStandardPaths::findExecutable("ping").isEmpty())
        return setState("error", "ping");
    const QString script = m_serverDirectory + "/ping_server.py";
    if (!QFileInfo::exists(script))
        return setState("error", "script", script);

    m_log.clear();
    m_failures = 0;
    m_stopping = false;
    m_process.setWorkingDirectory(m_serverDirectory);
    m_process.start(python, QStringList{script, "--port", QString::number(m_port)} + m_saved);
    setState("starting");
}

void Backend::stopServer() {
    if (m_process.state() == QProcess::NotRunning)
        return;
    m_stopping = true;
    m_process.terminate();
}

void Backend::toggleServer() {
    if (m_process.state() != QProcess::NotRunning)
        stopServer();
    else if (m_state != "external" || !m_reachable)
        launchServer();
}

QList<HostStats> Backend::parseStats(const QByteArray &json, bool *ok) {
    const auto document = QJsonDocument::fromJson(json);
    if (ok)
        *ok = document.isObject();
    QList<HostStats> hosts;
    const QJsonObject object = document.object();
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        HostStats h;
        h.name = it.key();
        h.status = o.value("status").toString("unknown");
        h.resolvedIp = o.value("resolved_ip").toString();
        h.average = o.value("avg_latency").toDouble(-1);
        h.minimum = o.value("min_latency").toDouble(-1);
        h.maximum = o.value("max_latency").toDouble(-1);
        h.loss = o.value("packet_loss_pct").toDouble(0);
        h.sent = o.value("total_sent").toInt();
        h.failures = o.value("consecutive_failures").toInt();
        // The server sends null for a dropped ping; -1 keeps the list numeric.
        for (const auto &value : o.value("history").toArray())
            h.history << (value.isNull() ? -1.0 : value.toDouble());
        h.last = h.history.isEmpty() ? -1 : h.history.last().toDouble();
        hosts << h;
    }
    return hosts;
}

void Backend::poll() {
    const bool listening = m_state == "starting" || m_state == "running" || m_state == "external";
    if (m_polling || !listening)
        return;
    m_polling = true;
    QNetworkRequest request(url("/stats"));
    request.setTransferTimeout(2000);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_polling = false;
        reply->deleteLater();
        bool ok = false;
        const QByteArray body = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
        const auto hosts = parseStats(body, &ok);
        if (ok) {
            m_failures = 0;
            const bool changed = !m_reachable || m_state == "starting";
            m_reachable = true;
            if (m_state == "starting")
                m_state = "running";
            m_model.setHosts(hosts);
            if (changed)
                emit serverChanged();
        } else if (++m_failures >= 3 && m_reachable) {
            // Keep the last numbers on screen, greyed out by the window.
            m_reachable = false;
            emit serverChanged();
        }
    });
}

// The host list in hosts.json is ours only when we run the server. Hosts on
// a server someone else started belong to that server.
bool Backend::ownsHosts() const {
    return m_process.state() != QProcess::NotRunning || m_state == "idle" || m_state == "error";
}

QString Backend::addHost(const QString &input) {
    const QString host = input.trimmed();
    if (host.isEmpty())
        return "empty";
    // The host becomes part of a URL path and a ping argument.
    static const QRegularExpression valid("^[A-Za-z0-9._:-]+$");
    if (!valid.match(host).hasMatch() || host.startsWith('-'))
        return "invalid";
    if (m_saved.contains(host) || m_model.indexOf(host) >= 0)
        return "duplicate";

    const bool owned = ownsHosts();
    if (owned) {
        m_saved << host;
        saveHosts();
    }
    if (m_reachable) {
        QNetworkRequest request(url("/hosts"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(2000);
        auto *reply = m_network.post(request, QJsonDocument(QJsonObject{{"host", host}}).toJson());
        connect(reply, &QNetworkReply::finished, this, [this, reply] { reply->deleteLater(); poll(); });
    } else if (owned && m_process.state() == QProcess::NotRunning) {
        showSavedHosts();
        launchServer();
    }
    return {};
}

void Backend::removeHost(const QString &host) {
    if (ownsHosts()) {
        m_saved.removeAll(host);
        saveHosts();
    }
    if (m_reachable) {
        QNetworkRequest request(url("/hosts/" + QString::fromLatin1(QUrl::toPercentEncoding(host))));
        request.setTransferTimeout(2000);
        auto *reply = m_network.deleteResource(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply] { reply->deleteLater(); poll(); });
    } else {
        showSavedHosts();
    }
}
