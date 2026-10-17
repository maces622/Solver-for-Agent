#include "solve_options.h"

#include <QSet>
#include <cmath>

namespace GtoApi {

namespace {

bool readInt(const QJsonObject &json,
             const QString &key,
             int defaultValue,
             int minimum,
             int maximum,
             int *value,
             QString *errorMessage)
{
    if (!json.contains(key)) {
        *value = defaultValue;
        return true;
    }
    const QJsonValue field = json.value(key);
    if (!field.isDouble()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("options.%1 must be an integer").arg(key);
        }
        return false;
    }
    const double number = field.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < minimum || number > maximum) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("options.%1 is out of range").arg(key);
        }
        return false;
    }
    *value = static_cast<int>(number);
    return true;
}

} // namespace

QJsonObject SolveOptions::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("maxIterations"), maxIterations);
    json.insert(QStringLiteral("targetExploitability"), targetExploitability);
    json.insert(QStringLiteral("threads"), threads);
    json.insert(QStringLiteral("algorithm"), algorithm);
    json.insert(QStringLiteral("printInterval"), printInterval);
    json.insert(QStringLiteral("dumpRounds"), dumpRounds);
    return json;
}

bool SolveOptions::fromJson(const QJsonObject &json,
                            SolveOptions *options,
                            QString *errorMessage)
{
    if (!options) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Solve options output object is null");
        }
        return false;
    }

    static const QSet<QString> allowedKeys = {
        QStringLiteral("maxIterations"),
        QStringLiteral("targetExploitability"),
        QStringLiteral("threads"),
        QStringLiteral("algorithm"),
        QStringLiteral("printInterval"),
        QStringLiteral("dumpRounds")
    };
    for (const QString &key : json.keys()) {
        if (!allowedKeys.contains(key)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unknown options field: %1").arg(key);
            }
            return false;
        }
    }

    SolveOptions parsed;
    if (!readInt(json, QStringLiteral("maxIterations"), parsed.maxIterations,
                 1, 10000000, &parsed.maxIterations, errorMessage)
        || !readInt(json, QStringLiteral("threads"), parsed.threads,
                    -1, 1024, &parsed.threads, errorMessage)
        || !readInt(json, QStringLiteral("printInterval"), parsed.printInterval,
                    1, 1000000, &parsed.printInterval, errorMessage)
        || !readInt(json, QStringLiteral("dumpRounds"), parsed.dumpRounds,
                    0, 3, &parsed.dumpRounds, errorMessage)) {
        return false;
    }
    if (parsed.threads == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("options.threads must be -1 or between 1 and 1024");
        }
        return false;
    }

    if (json.contains(QStringLiteral("targetExploitability"))) {
        const QJsonValue value = json.value(QStringLiteral("targetExploitability"));
        if (!value.isDouble() || !std::isfinite(value.toDouble())
            || value.toDouble() <= 0.0 || value.toDouble() > 1000000.0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("options.targetExploitability must be a positive number");
            }
            return false;
        }
        parsed.targetExploitability = value.toDouble();
    }

    if (json.contains(QStringLiteral("algorithm"))) {
        const QJsonValue value = json.value(QStringLiteral("algorithm"));
        if (!value.isString() || value.toString().isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("options.algorithm must be a non-empty string");
            }
            return false;
        }
        parsed.algorithm = value.toString();
    }
    if (parsed.algorithm != QStringLiteral("discounted_cfr")) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported algorithm: %1").arg(parsed.algorithm);
        }
        return false;
    }

    *options = parsed;
    return true;
}

} // namespace GtoApi