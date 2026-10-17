#include "state_store.h"

namespace GtoApi {

StateStore::StateStore(const QString &sessionId, QObject *parent)
    : QObject(parent)
{
    m_snapshot.sessionId = sessionId;
}

StateSnapshot StateStore::snapshot() const
{
    return m_snapshot;
}

QString StateStore::sessionId() const
{
    return m_snapshot.sessionId;
}

qint64 StateStore::revision() const
{
    return m_snapshot.revision;
}

bool StateStore::setSpot(const SpotSpec &spot)
{
    return updateFromGui(spot, m_snapshot.tree, m_snapshot.solve, m_snapshot.selection);
}

bool StateStore::setSelection(const QString &nodeId)
{
    QJsonObject selection;
    selection.insert(QStringLiteral("nodeId"), nodeId);
    return updateFromGui(m_snapshot.spot, m_snapshot.tree,
                         m_snapshot.solve, selection);
}

bool StateStore::updateFromGui(const SpotSpec &spot,
                               const QJsonValue &tree,
                               const QJsonValue &solve,
                               const QJsonValue &selection)
{
    QStringList changedPaths = changedSpotPaths(m_snapshot.spot, spot);
    if (m_snapshot.tree != tree) {
        changedPaths.append(QStringLiteral("tree"));
    }
    if (m_snapshot.solve != solve) {
        changedPaths.append(QStringLiteral("solve"));
    }
    if (m_snapshot.selection != selection) {
        changedPaths.append(QStringLiteral("selection"));
    }

    if (changedPaths.isEmpty()) {
        return false;
    }

    m_snapshot.spot = spot;
    m_snapshot.tree = tree;
    m_snapshot.solve = solve;
    m_snapshot.selection = selection;
    ++m_snapshot.revision;
    emit stateChanged(m_snapshot.revision, changedPaths);
    return true;
}

} // namespace GtoApi