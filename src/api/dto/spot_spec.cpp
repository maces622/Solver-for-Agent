#include "spot_spec.h"

#include <QJsonArray>
#include <QSet>

namespace GtoApi {

namespace {

QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QJsonArray toJsonArray(const QList<double> &values)
{
    QJsonArray array;
    for (double value : values) {
        array.append(value);
    }
    return array;
}

} // namespace

bool SpotSpec::fromJson(const QJsonObject &json, SpotSpec *spot, QString *errorMessage)
{
    if (!spot) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Spot output object is null");
        }
        return false;
    }

    auto fail = [errorMessage](const QString &message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    static const QSet<QString> allowedKeys = {
        QStringLiteral("gameType"),
        QStringLiteral("mode"),
        QStringLiteral("positions"),
        QStringLiteral("effectiveStackBb"),
        QStringLiteral("blinds"),
        QStringLiteral("pot"),
        QStringLiteral("board"),
        QStringLiteral("ranges"),
        QStringLiteral("betSizing"),
        QStringLiteral("allinThreshold"),
        QStringLiteral("useIsomorphism"),
        QStringLiteral("useHalfFloats")
    };
    for (const QString &key : json.keys()) {
        if (!allowedKeys.contains(key)) {
            return fail(QStringLiteral("Unknown spot field: %1").arg(key));
        }
    }

    auto requireString = [&json, &fail](const QString &key, QString *value) {
        const QJsonValue field = json.value(key);
        if (!field.isString()) {
            return fail(QStringLiteral("spot.%1 must be a string").arg(key));
        }
        *value = field.toString();
        return true;
    };

    auto requireDouble = [&json, &fail](const QString &key, double *value) {
        const QJsonValue field = json.value(key);
        if (!field.isDouble()) {
            return fail(QStringLiteral("spot.%1 must be a number").arg(key));
        }
        *value = field.toDouble();
        return true;
    };

    auto requireBool = [&json, &fail](const QString &key, bool *value) {
        const QJsonValue field = json.value(key);
        if (!field.isBool()) {
            return fail(QStringLiteral("spot.%1 must be a boolean").arg(key));
        }
        *value = field.toBool();
        return true;
    };

    auto requireStringArray = [&json, &fail](const QString &key, QStringList *values) {
        const QJsonValue field = json.value(key);
        if (!field.isArray()) {
            return fail(QStringLiteral("spot.%1 must be an array of strings").arg(key));
        }
        values->clear();
        const QJsonArray array = field.toArray();
        for (const QJsonValue &value : array) {
            if (!value.isString()) {
                return fail(QStringLiteral("spot.%1 must contain only strings").arg(key));
            }
            values->append(value.toString());
        }
        return true;
    };

    auto requireDoubleArray = [&json, &fail](const QString &key, QList<double> *values) {
        const QJsonValue field = json.value(key);
        if (!field.isArray()) {
            return fail(QStringLiteral("spot.%1 must be an array of numbers").arg(key));
        }
        values->clear();
        const QJsonArray array = field.toArray();
        for (const QJsonValue &value : array) {
            if (!value.isDouble()) {
                return fail(QStringLiteral("spot.%1 must contain only numbers").arg(key));
            }
            values->append(value.toDouble());
        }
        return true;
    };

    auto requireObject = [&json, &fail](const QString &key, QJsonObject *value) {
        const QJsonValue field = json.value(key);
        if (!field.isObject()) {
            return fail(QStringLiteral("spot.%1 must be an object").arg(key));
        }
        *value = field.toObject();
        return true;
    };

    SpotSpec parsed;
    if (!requireString(QStringLiteral("gameType"), &parsed.gameType)
        || !requireString(QStringLiteral("mode"), &parsed.mode)
        || !requireStringArray(QStringLiteral("positions"), &parsed.positions)
        || !requireDouble(QStringLiteral("effectiveStackBb"), &parsed.effectiveStackBb)
        || !requireDoubleArray(QStringLiteral("blinds"), &parsed.blinds)
        || !requireDouble(QStringLiteral("pot"), &parsed.pot)
        || !requireStringArray(QStringLiteral("board"), &parsed.board)
        || !requireObject(QStringLiteral("ranges"), &parsed.ranges)
        || !requireObject(QStringLiteral("betSizing"), &parsed.betSizing)
        || !requireDouble(QStringLiteral("allinThreshold"), &parsed.allinThreshold)
        || !requireBool(QStringLiteral("useIsomorphism"), &parsed.useIsomorphism)
        || !requireBool(QStringLiteral("useHalfFloats"), &parsed.useHalfFloats)) {
        return false;
    }

    *spot = parsed;
    return true;
}
QJsonObject SpotSpec::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("gameType"), gameType);
    json.insert(QStringLiteral("mode"), mode);
    json.insert(QStringLiteral("positions"), toJsonArray(positions));
    json.insert(QStringLiteral("effectiveStackBb"), effectiveStackBb);
    json.insert(QStringLiteral("blinds"), toJsonArray(blinds));
    json.insert(QStringLiteral("pot"), pot);
    json.insert(QStringLiteral("board"), toJsonArray(board));
    json.insert(QStringLiteral("ranges"), ranges);
    json.insert(QStringLiteral("betSizing"), betSizing);
    json.insert(QStringLiteral("allinThreshold"), allinThreshold);
    json.insert(QStringLiteral("useIsomorphism"), useIsomorphism);
    json.insert(QStringLiteral("useHalfFloats"), useHalfFloats);
    return json;
}

bool SpotSpec::operator==(const SpotSpec &other) const
{
    return toJson() == other.toJson();
}

QStringList changedSpotPaths(const SpotSpec &previous, const SpotSpec &current)
{
    const QJsonObject before = previous.toJson();
    const QJsonObject after = current.toJson();
    static const QStringList keys = {
        QStringLiteral("gameType"),
        QStringLiteral("mode"),
        QStringLiteral("positions"),
        QStringLiteral("effectiveStackBb"),
        QStringLiteral("blinds"),
        QStringLiteral("pot"),
        QStringLiteral("board"),
        QStringLiteral("ranges"),
        QStringLiteral("betSizing"),
        QStringLiteral("allinThreshold"),
        QStringLiteral("useIsomorphism"),
        QStringLiteral("useHalfFloats")
    };

    QStringList changedPaths;
    for (const QString &key : keys) {
        if (before.value(key) != after.value(key)) {
            changedPaths.append(QStringLiteral("spot.") + key);
        }
    }
    return changedPaths;
}

} // namespace GtoApi