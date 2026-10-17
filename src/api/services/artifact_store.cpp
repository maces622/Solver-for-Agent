#include "artifact_store.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace GtoApi {

ArtifactStore::ArtifactStore(QObject *parent)
    : ArtifactStore(defaultRootPath(), parent)
{
}

ArtifactStore::ArtifactStore(const QString &rootPath, QObject *parent)
    : QObject(parent)
    , m_rootPath(rootPath)
{
}

QString ArtifactStore::rootPath() const
{
    return m_rootPath;
}

QString ArtifactStore::defaultRootPath()
{
    const QString overrideRoot = qEnvironmentVariable("GTO_API_ARTIFACT_ROOT");
    if (!overrideRoot.isEmpty()) {
        return overrideRoot;
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath();
    }
    return QDir(base).filePath(QStringLiteral("artifacts"));
}

bool ArtifactStore::store(const QString &jobId,
                          const QJsonObject &content,
                          ArtifactRef *artifact,
                          QString *errorMessage)
{
    if (!artifact) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Artifact output object is null");
        }
        return false;
    }

    static const QRegularExpression safeIdPattern(QStringLiteral("^[A-Za-z0-9_-]+$"));
    const QString safeJobId = safeIdPattern.match(jobId).hasMatch()
                                  ? jobId
                                  : QStringLiteral("unknown-job");
    const QString artifactId = QStringLiteral("artifact-")
                               + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString directoryPath = QDir(m_rootPath).filePath(safeJobId);
    if (!QDir().mkpath(directoryPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create artifact directory");
        }
        return false;
    }

    const QByteArray contentBytes = QJsonDocument(content).toJson(QJsonDocument::Compact);
    const QString filePath = QDir(directoryPath).filePath(artifactId + QStringLiteral(".json"));
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open artifact file: %1").arg(file.errorString());
        }
        return false;
    }
    if (file.write(contentBytes) != contentBytes.size() || !file.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot write artifact file: %1").arg(file.errorString());
        }
        return false;
    }

    ArtifactRef ref;
    ref.artifactId = artifactId;
    ref.uri = QStringLiteral("artifact://%1/%2").arg(safeJobId, artifactId);
    ref.mimeType = QStringLiteral("application/json");
    ref.sizeBytes = contentBytes.size();
    ref.sha256 = QString::fromLatin1(
        QCryptographicHash::hash(contentBytes, QCryptographicHash::Sha256).toHex());
    ref.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_artifacts.insert(ref.artifactId, ref);
    *artifact = ref;
    return true;
}

bool ArtifactStore::getMetadata(const QString &artifactId,
                                ArtifactRef *artifact,
                                QString *errorMessage) const
{
    auto it = m_artifacts.constFind(artifactId);
    if (it == m_artifacts.constEnd()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Artifact was not found");
        }
        return false;
    }
    if (artifact) {
        *artifact = it.value();
    }
    return true;
}

} // namespace GtoApi