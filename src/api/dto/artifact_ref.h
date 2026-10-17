#ifndef GTO_API_ARTIFACT_REF_H
#define GTO_API_ARTIFACT_REF_H

#include <QJsonObject>
#include <QString>

namespace GtoApi {

struct ArtifactRef
{
    QString artifactId;
    QString uri;
    QString mimeType;
    qint64 sizeBytes = 0;
    QString sha256;
    QString createdAt;

    QJsonObject toJson() const;
};

} // namespace GtoApi

#endif // GTO_API_ARTIFACT_REF_H