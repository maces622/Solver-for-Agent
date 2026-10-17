#ifndef GTO_API_SPOT_VALIDATOR_H
#define GTO_API_SPOT_VALIDATOR_H

#include "../dto/spot_spec.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace GtoApi {

struct ValidationIssue
{
    QString path;
    QString code;
    QString message;

    QJsonObject toJson() const;
};

struct RangeValidationResult
{
    bool valid = false;
    QString normalizedRange;
    int comboCount = 0;
    QStringList warnings;
    QList<ValidationIssue> errors;

    QJsonObject toJson() const;
};

struct SpotValidationResult
{
    bool valid = false;
    SpotSpec normalizedSpot;
    QStringList warnings;
    QList<ValidationIssue> errors;

    QJsonObject toJson() const;
};

class SpotValidator
{
public:
    static SpotValidationResult validate(const SpotSpec &spot);
    static RangeValidationResult validateRange(const QString &range,
                                               const QStringList &board,
                                               const QString &mode);
};

} // namespace GtoApi

#endif // GTO_API_SPOT_VALIDATOR_H