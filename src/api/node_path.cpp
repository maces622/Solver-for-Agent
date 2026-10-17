#include "node_path.h"

#include <QRegularExpression>
#include <QStringList>

namespace GtoApi {

bool isValidNodePathSyntax(const QString &nodeId, QString *errorMessage)
{
    if (nodeId == QStringLiteral("root")) {
        return true;
    }
    if (!nodeId.startsWith(QStringLiteral("root/"))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Node id must be root or start with root/");
        }
        return false;
    }

    const QStringList segments = nodeId.split(QLatin1Char('/'));
    if (segments.size() < 2 || segments.first() != QStringLiteral("root")) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Node id must start with root");
        }
        return false;
    }

    static const QRegularExpression actionSegment(QStringLiteral("^a\\d+$"));
    static const QRegularExpression chanceSegment(QStringLiteral("^c.+$"));
    for (int index = 1; index < segments.size(); ++index) {
        const QString &segment = segments.at(index);
        if (segment.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Node path contains an empty segment");
            }
            return false;
        }
        if (!actionSegment.match(segment).hasMatch()
            && !chanceSegment.match(segment).hasMatch()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Invalid node path segment: %1").arg(segment);
            }
            return false;
        }
    }
    return true;
}

} // namespace GtoApi
