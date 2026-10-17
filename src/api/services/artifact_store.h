#ifndef GTO_API_ARTIFACT_STORE_H
#define GTO_API_ARTIFACT_STORE_H

#include "../dto/artifact_ref.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace GtoApi {

class ArtifactStore : public QObject
{
    Q_OBJECT

public:
    explicit ArtifactStore(QObject *parent = nullptr);
    explicit ArtifactStore(const QString &rootPath, QObject *parent = nullptr);

    QString rootPath() const;
    bool store(const QString &jobId,
               const QJsonObject &content,
               ArtifactRef *artifact,
               QString *errorMessage = nullptr);
    bool getMetadata(const QString &artifactId,
                     ArtifactRef *artifact,
                     QString *errorMessage = nullptr) const;

    static QString defaultRootPath();

private:
    QString m_rootPath;
    QHash<QString, ArtifactRef> m_artifacts;
};

} // namespace GtoApi

#endif // GTO_API_ARTIFACT_STORE_H