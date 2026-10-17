#include "artifact_ref.h"

namespace GtoApi {

QJsonObject ArtifactRef::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("artifactId"), artifactId);
    json.insert(QStringLiteral("uri"), uri);
    json.insert(QStringLiteral("mimeType"), mimeType);
    json.insert(QStringLiteral("sizeBytes"), static_cast<double>(sizeBytes));
    json.insert(QStringLiteral("sha256"), sha256);
    json.insert(QStringLiteral("createdAt"), createdAt);
    return json;
}

} // namespace GtoApi