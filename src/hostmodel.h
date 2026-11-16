#pragma once

#include <QAbstractListModel>
#include <QVariantList>

// One monitored host, as the server reports it. A latency of -1 means "none":
// a dropped ping, or no pings yet.
struct HostStats {
    QString name;
    QString status = "unknown";
    QString resolvedIp;
    double last = -1, average = -1, minimum = -1, maximum = -1, loss = 0;
    int sent = 0, failures = 0;
    QVariantList history;
};

// A model rather than a plain list so the view keeps its scroll position and
// selection while the numbers change every second.
class HostModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { NameRole = Qt::UserRole + 1, StatusRole, ResolvedIpRole, LastRole, AverageRole, MinimumRole,
                MaximumRole, LossRole, SentRole, FailuresRole, HistoryRole };

    explicit HostModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_hosts.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_hosts.size(); }
    QStringList names() const;
    void setHosts(const QList<HostStats> &hosts);

    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE int indexOf(const QString &name) const;

signals:
    void countChanged();

private:
    QList<HostStats> m_hosts;
};
