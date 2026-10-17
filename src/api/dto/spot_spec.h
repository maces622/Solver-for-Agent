#ifndef GTO_API_SPOT_SPEC_H
#define GTO_API_SPOT_SPEC_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace GtoApi {

class SpotSpec
{
public:
    QString gameType = QStringLiteral("NLHE");
    QString mode = QStringLiteral("holdem");
    QStringList positions;
    double effectiveStackBb = 0.0;
    QList<double> blinds;
    double pot = 0.0;
    QStringList board;
    QJsonObject ranges;
    QJsonObject betSizing;
    double allinThreshold = 0.67;
    bool useIsomorphism = true;
    bool useHalfFloats = false;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &json, SpotSpec *spot, QString *errorMessage = nullptr);
    bool operator==(const SpotSpec &other) const;
    bool operator!=(const SpotSpec &other) const { return !(*this == other); }
};

QStringList changedSpotPaths(const SpotSpec &previous, const SpotSpec &current);

} // namespace GtoApi

#endif // GTO_API_SPOT_SPEC_H