#ifndef GTO_API_STATE_STORE_H
#define GTO_API_STATE_STORE_H

#include "../dto/state_snapshot.h"

#include <QObject>
#include <QStringList>

namespace GtoApi {

class StateStore : public QObject
{
    Q_OBJECT

public:
    explicit StateStore(const QString &sessionId, QObject *parent = nullptr);

    StateSnapshot snapshot() const;
    QString sessionId() const;
    qint64 revision() const;

    bool setSpot(const SpotSpec &spot);
    bool setSelection(const QString &nodeId);
    bool updateFromGui(const SpotSpec &spot,
                       const QJsonValue &tree,
                       const QJsonValue &solve,
                       const QJsonValue &selection);

signals:
    void stateChanged(qint64 revision, const QStringList &changedPaths);

private:
    StateSnapshot m_snapshot;
};

} // namespace GtoApi

#endif // GTO_API_STATE_STORE_H