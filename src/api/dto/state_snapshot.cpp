#include "state_snapshot.h"

namespace GtoApi {

QJsonObject StateSnapshot::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("revision"), static_cast<double>(revision));
    json.insert(QStringLiteral("sessionId"), sessionId);
    json.insert(QStringLiteral("screen"), screen);
    json.insert(QStringLiteral("spot"), spot.toJson());
    json.insert(QStringLiteral("tree"), tree);
    json.insert(QStringLiteral("solve"), solve);
    json.insert(QStringLiteral("selection"), selection);
    return json;
}

} // namespace GtoApi