#ifndef GTO_API_STATE_SNAPSHOT_H
#define GTO_API_STATE_SNAPSHOT_H

#include "spot_spec.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace GtoApi {

struct StateSnapshot
{
    qint64 revision = 0;
    QString sessionId;
    QString screen = QStringLiteral("main");
    SpotSpec spot;
    QJsonValue tree = QJsonValue::Null;
    QJsonValue solve = QJsonValue::Null;
    QJsonValue selection = QJsonValue::Null;

    QJsonObject toJson() const;
};

} // namespace GtoApi

#endif // GTO_API_STATE_SNAPSHOT_H