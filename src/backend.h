#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTimer>

#include "hostmodel.h"

// Runs the ping-monitor server (or attaches to one that is already running)
// and turns its /stats into a model for the window.
//
// State and error kinds are plain codes so QML can translate them, and a
// language switch re-renders them live.
//   serverState: idle, starting, running (we started it), external (someone else did), error
//   errorKind:   python, ping, script, exited (with errorDetail), or empty
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(HostModel *hosts READ hosts CONSTANT)
    Q_PROPERTY(QString serverState READ serverState NOTIFY serverChanged)
    Q_PROPERTY(QString errorKind READ errorKind NOTIFY serverChanged)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY serverChanged)
    Q_PROPERTY(bool reachable READ reachable NOTIFY serverChanged)
    Q_PROPERTY(int port READ port CONSTANT)
    Q_PROPERTY(int goodMs READ goodMs CONSTANT)
    Q_PROPERTY(int warnMs READ warnMs CONSTANT)

public:
    // Every path can be overridden so tests never touch the real config.
    explicit Backend(const QString &configDirectory = {}, const QString &serverDirectory = {},
                     const QString &monitorConfig = {}, QObject *parent = nullptr);
    ~Backend() override;

    HostModel *hosts() { return &m_model; }
    QString serverState() const { return m_state; }
    QString errorKind() const { return m_errorKind; }
    QString errorDetail() const { return m_errorDetail; }
    bool reachable() const { return m_reachable; }
    int port() const { return m_port; }
    int goodMs() const { return m_good; }
    int warnMs() const { return m_warn; }

    // Probes the port, attaches if a server answers, and starts our own if not.
    Q_INVOKABLE void start();
    Q_INVOKABLE void stopServer();
    Q_INVOKABLE void toggleServer();
    // Both return an error kind (empty, invalid, duplicate) or an empty string.
    Q_INVOKABLE QString addHost(const QString &host);
    Q_INVOKABLE void removeHost(const QString &host);

    static QList<HostStats> parseStats(const QByteArray &json, bool *ok = nullptr);
    static QStringList loadHosts(const QString &path);

signals:
    void serverChanged();

private:
    void launchServer();
    void poll();
    void setState(const QString &state, const QString &kind = {}, const QString &detail = {});
    bool ownsHosts() const;
    void saveHosts() const;
    void showSavedHosts();
    QUrl url(const QString &path) const;

    QString m_configDirectory, m_serverDirectory;
    int m_port = 8765, m_good = 50, m_warn = 150;
    QStringList m_saved;

    HostModel m_model;
    QNetworkAccessManager m_network;
    QProcess m_process;
    QTimer m_timer;
    QString m_state = "idle", m_errorKind, m_errorDetail, m_log;
    bool m_reachable = false, m_polling = false, m_stopping = false;
    int m_failures = 0;
};
