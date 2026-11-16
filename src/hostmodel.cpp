#include "hostmodel.h"

QHash<int, QByteArray> HostModel::roleNames() const {
    return {{NameRole, "name"}, {StatusRole, "status"}, {ResolvedIpRole, "resolvedIp"}, {LastRole, "last"},
            {AverageRole, "average"}, {MinimumRole, "minimum"}, {MaximumRole, "maximum"}, {LossRole, "loss"},
            {SentRole, "sent"}, {FailuresRole, "failures"}, {HistoryRole, "history"}};
}

QVariant HostModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_hosts.size())
        return {};
    const HostStats &h = m_hosts.at(index.row());
    switch (role) {
    case NameRole: return h.name;
    case StatusRole: return h.status;
    case ResolvedIpRole: return h.resolvedIp;
    case LastRole: return h.last;
    case AverageRole: return h.average;
    case MinimumRole: return h.minimum;
    case MaximumRole: return h.maximum;
    case LossRole: return h.loss;
    case SentRole: return h.sent;
    case FailuresRole: return h.failures;
    case HistoryRole: return h.history;
    }
    return {};
}

QStringList HostModel::names() const {
    QStringList names;
    for (const auto &h : m_hosts)
        names << h.name;
    return names;
}

void HostModel::setHosts(const QList<HostStats> &hosts) {
    QStringList incoming;
    for (const auto &h : hosts)
        incoming << h.name;

    // Same hosts in the same order is the every-second case: update in place.
    // Anything else is rare (a host added or removed), so a reset is fine.
    const bool sameShape = incoming == names();
    if (!sameShape)
        beginResetModel();
    const int before = m_hosts.size();
    m_hosts = hosts;
    if (sameShape) {
        if (!m_hosts.isEmpty())
            emit dataChanged(index(0), index(m_hosts.size() - 1));
    } else {
        endResetModel();
    }
    if (before != m_hosts.size())
        emit countChanged();
}

QString HostModel::nameAt(int row) const {
    return row >= 0 && row < m_hosts.size() ? m_hosts.at(row).name : QString();
}

int HostModel::indexOf(const QString &name) const {
    for (int i = 0; i < m_hosts.size(); ++i)
        if (m_hosts.at(i).name == name)
            return i;
    return -1;
}
