#include "spot_validator.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace GtoApi {

namespace {

const QString kRanks = QStringLiteral("23456789TJQKA");
const QString kSuits = QStringLiteral("cdhs");

void addIssue(QList<ValidationIssue> *issues,
              const QString &path,
              const QString &code,
              const QString &message)
{
    ValidationIssue issue;
    issue.path = path;
    issue.code = code;
    issue.message = message;
    issues->append(issue);
}

bool isFiniteNumber(double value)
{
    return std::isfinite(value);
}

int rankIndex(QChar rank)
{
    return kRanks.indexOf(rank.toUpper());
}

int suitIndex(QChar suit)
{
    return kSuits.indexOf(suit.toLower());
}

bool rankAllowed(QChar rank, bool shortdeck)
{
    const int index = rankIndex(rank);
    if (index < 0) {
        return false;
    }
    return !shortdeck || kRanks.at(index).digitValue() >= 6
           || kRanks.at(index) == QLatin1Char('T')
           || kRanks.at(index) == QLatin1Char('J')
           || kRanks.at(index) == QLatin1Char('Q')
           || kRanks.at(index) == QLatin1Char('K')
           || kRanks.at(index) == QLatin1Char('A');
}

bool parseBoard(const QStringList &board,
                bool shortdeck,
                QStringList *normalizedBoard,
                QSet<int> *boardCards,
                QList<ValidationIssue> *issues,
                const QString &path)
{
    normalizedBoard->clear();
    boardCards->clear();
    if (board.size() > 5) {
        addIssue(issues, path, QStringLiteral("BOARD_TOO_MANY_CARDS"),
                 QStringLiteral("Board must contain at most 5 cards"));
    }

    for (int index = 0; index < board.size(); ++index) {
        const QString card = board.at(index).trimmed();
        if (card.size() != 2) {
            addIssue(issues, QStringLiteral("%1[%2]").arg(path).arg(index),
                     QStringLiteral("INVALID_BOARD_CARD"),
                     QStringLiteral("Board card must use rank+suit format, for example Ah"));
            continue;
        }

        const QChar rank = card.at(0).toUpper();
        const QChar suit = card.at(1).toLower();
        if (!rankAllowed(rank, shortdeck) || suitIndex(suit) < 0) {
            addIssue(issues, QStringLiteral("%1[%2]").arg(path).arg(index),
                     QStringLiteral("INVALID_BOARD_CARD"),
                     QStringLiteral("Invalid board card: %1").arg(card));
            continue;
        }

        const int cardId = rankIndex(rank) * 4 + suitIndex(suit);
        if (boardCards->contains(cardId)) {
            addIssue(issues, QStringLiteral("%1[%2]").arg(path).arg(index),
                     QStringLiteral("DUPLICATE_BOARD_CARD"),
                     QStringLiteral("Duplicate board card: %1").arg(card));
            continue;
        }
        boardCards->insert(cardId);
        normalizedBoard->append(QString(rank) + QString(suit));
    }
    return issues->isEmpty();
}

QJsonArray issuesToJson(const QList<ValidationIssue> &issues)
{
    QJsonArray array;
    for (const ValidationIssue &issue : issues) {
        array.append(issue.toJson());
    }
    return array;
}

QJsonArray warningsToJson(const QStringList &warnings)
{
    return QJsonArray::fromStringList(warnings);
}

} // namespace

QJsonObject ValidationIssue::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("path"), path);
    json.insert(QStringLiteral("code"), code);
    json.insert(QStringLiteral("message"), message);
    return json;
}

QJsonObject RangeValidationResult::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("valid"), valid);
    json.insert(QStringLiteral("normalizedRange"),
                valid ? QJsonValue(normalizedRange) : QJsonValue::Null);
    json.insert(QStringLiteral("comboCount"), comboCount);
    json.insert(QStringLiteral("warnings"), warningsToJson(warnings));
    json.insert(QStringLiteral("errors"), issuesToJson(errors));
    return json;
}

QJsonObject SpotValidationResult::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("valid"), valid);
    json.insert(QStringLiteral("normalizedSpot"),
                valid ? QJsonValue(normalizedSpot.toJson()) : QJsonValue::Null);
    json.insert(QStringLiteral("warnings"), warningsToJson(warnings));
    json.insert(QStringLiteral("errors"), issuesToJson(errors));
    return json;
}
namespace {

bool validateRangeInternal(const QString &range,
                           const QSet<int> &boardCards,
                           bool shortdeck,
                           QString *normalizedRange,
                           int *comboCount,
                           QStringList *warnings,
                           QList<ValidationIssue> *issues,
                           const QString &path)
{
    normalizedRange->clear();
    *comboCount = 0;

    if (range.trimmed().isEmpty()) {
        addIssue(issues, path, QStringLiteral("RANGE_EMPTY"),
                 QStringLiteral("Range must not be empty"));
        return false;
    }
    if (range.size() > 16384) {
        addIssue(issues, path, QStringLiteral("RANGE_TOO_LARGE"),
                 QStringLiteral("Range string exceeds the 16 KiB limit"));
        return false;
    }

    const QStringList tokens = range.split(QLatin1Char(','), Qt::KeepEmptyParts);
    QStringList normalizedTokens;
    QSet<int> seenCombos;
    int skippedByBoard = 0;
    static const QRegularExpression handPattern(
        QStringLiteral("^([2-9TJQKA])([2-9TJQKA])([so])?$"),
        QRegularExpression::CaseInsensitiveOption);

    for (int tokenIndex = 0; tokenIndex < tokens.size(); ++tokenIndex) {
        const QString rawToken = tokens.at(tokenIndex).trimmed();
        if (rawToken.isEmpty()) {
            continue;
        }

        const QStringList weightParts = rawToken.split(QLatin1Char(':'));
        if (weightParts.size() > 2) {
            addIssue(issues, QStringLiteral("%1[%2]").arg(path).arg(tokenIndex),
                     QStringLiteral("RANGE_SYNTAX"),
                     QStringLiteral("Range token contains more than one ':'"));
            continue;
        }

        const QString hand = weightParts.at(0).trimmed().toUpper();
        const QRegularExpressionMatch match = handPattern.match(hand);
        if (!match.hasMatch()) {
            addIssue(issues, QStringLiteral("%1[%2]").arg(path).arg(tokenIndex),
                     QStringLiteral("RANGE_SYNTAX"),
                     QStringLiteral("Invalid range token: %1").arg(rawToken));
            continue;
        }

        const QChar rank1 = match.captured(1).at(0);
        const QChar rank2 = match.captured(2).at(0);
        if (!rankAllowed(rank1, shortdeck) || !rankAllowed(rank2, shortdeck)) {
            addIssue(issues, QStringLiteral("%1[%2]").arg(path).arg(tokenIndex),
                     QStringLiteral("RANGE_RANK_UNSUPPORTED"),
                     QStringLiteral("Range token contains a rank unsupported by this mode: %1")
                         .arg(rawToken));
            continue;
        }

        const QChar suffix = match.captured(3).isEmpty()
                                 ? QChar()
                                 : match.captured(3).at(0).toLower();
        if (rank1 == rank2 && !suffix.isNull()) {
            addIssue(issues, QStringLiteral("%1[%2]").arg(path).arg(tokenIndex),
                     QStringLiteral("RANGE_SYNTAX"),
                     QStringLiteral("A pocket pair cannot use an s/o suffix: %1")
                         .arg(rawToken));
            continue;
        }

        double weight = 1.0;
        if (weightParts.size() == 2) {
            bool weightOk = false;
            weight = weightParts.at(1).trimmed().toDouble(&weightOk);
            if (!weightOk || !isFiniteNumber(weight) || weight < 0.0 || weight > 1.0) {
                addIssue(issues, QStringLiteral("%1[%2]").arg(path).arg(tokenIndex),
                         QStringLiteral("RANGE_WEIGHT"),
                         QStringLiteral("Range weight must be between 0 and 1"));
                continue;
            }
        }

        QString normalizedToken = QString(rank1) + QString(rank2);
        if (!suffix.isNull()) {
            normalizedToken += suffix;
        }
        if (weightParts.size() == 2) {
            normalizedToken += QStringLiteral(":") + QString::number(weight, 'g', 12);
        }
        normalizedTokens.append(normalizedToken);

        if (weight <= 0.005) {
            warnings->append(QStringLiteral("Range token %1 has zero weight and was ignored")
                                 .arg(rawToken));
            continue;
        }

        const int rankA = rankIndex(rank1);
        const int rankB = rankIndex(rank2);
        for (int suitA = 0; suitA < 4; ++suitA) {
            for (int suitB = 0; suitB < 4; ++suitB) {
                if (rank1 == rank2 && suitA >= suitB) {
                    continue;
                }
                if (rank1 != rank2) {
                    if (suffix == QLatin1Char('s') && suitA != suitB) {
                        continue;
                    }
                    if (suffix == QLatin1Char('o') && suitA == suitB) {
                        continue;
                    }
                }

                int cardA = rankA * 4 + suitA;
                int cardB = rankB * 4 + suitB;
                if (cardA > cardB) {
                    qSwap(cardA, cardB);
                }
                if (boardCards.contains(cardA) || boardCards.contains(cardB)) {
                    ++skippedByBoard;
                    continue;
                }

                const int comboId = cardA * 52 + cardB;
                if (seenCombos.contains(comboId)) {
                    addIssue(issues, QStringLiteral("%1[%2]").arg(path).arg(tokenIndex),
                             QStringLiteral("RANGE_DUPLICATE_COMBO"),
                             QStringLiteral("Duplicate hand combination produced by token: %1")
                                 .arg(rawToken));
                } else {
                    seenCombos.insert(comboId);
                    ++(*comboCount);
                }
            }
        }
    }

    if (normalizedTokens.isEmpty()) {
        addIssue(issues, path, QStringLiteral("RANGE_EMPTY"),
                 QStringLiteral("Range does not contain any valid hand tokens"));
    }
    if (skippedByBoard > 0) {
        warnings->append(QStringLiteral("%1 combination(s) in %2 were blocked by the board")
                             .arg(skippedByBoard)
                             .arg(path));
    }
    if (*comboCount == 0 && !normalizedTokens.isEmpty()) {
        addIssue(issues, path, QStringLiteral("RANGE_EMPTY"),
                 QStringLiteral("Range contains no combinations after board filtering"));
    }

    *normalizedRange = normalizedTokens.join(QLatin1Char(','));
    return issues->isEmpty();
}

} // namespace
namespace {

bool validateSizingSide(const QJsonObject &side,
                        const QString &path,
                        QJsonObject *normalized,
                        QList<ValidationIssue> *issues)
{
    static const QSet<QString> allowedKeys = {
        QStringLiteral("bet"),
        QStringLiteral("raise"),
        QStringLiteral("donk"),
        QStringLiteral("allin")
    };
    for (const QString &key : side.keys()) {
        if (!allowedKeys.contains(key)) {
            addIssue(issues, path + QLatin1Char('.') + key,
                     QStringLiteral("BET_SIZING_UNKNOWN_FIELD"),
                     QStringLiteral("Unknown bet sizing field: %1").arg(key));
        }
    }

    QJsonObject result;
    for (const QString &key : {QStringLiteral("bet"), QStringLiteral("raise")}) {
        const QJsonValue value = side.value(key);
        if (!value.isArray()) {
            addIssue(issues, path + QLatin1Char('.') + key,
                     QStringLiteral("BET_SIZING_TYPE"),
                     QStringLiteral("%1 must be an array of numbers").arg(key));
            continue;
        }
        const QJsonArray sizes = value.toArray();
        if (sizes.size() > 20) {
            addIssue(issues, path + QLatin1Char('.') + key,
                     QStringLiteral("BET_SIZING_TOO_MANY"),
                     QStringLiteral("%1 contains more than 20 sizes").arg(key));
        }
        QJsonArray normalizedSizes;
        for (int index = 0; index < sizes.size(); ++index) {
            const QJsonValue size = sizes.at(index);
            if (!size.isDouble() || !isFiniteNumber(size.toDouble())
                || size.toDouble() < 0.0 || size.toDouble() > 1000.0) {
                addIssue(issues, QStringLiteral("%1.%2[%3]").arg(path, key).arg(index),
                         QStringLiteral("BET_SIZING_VALUE"),
                         QStringLiteral("Bet size must be a number between 0 and 1000"));
                continue;
            }
            normalizedSizes.append(size.toDouble());
        }
        result.insert(key, normalizedSizes);
    }

    const QJsonValue donkValue = side.value(QStringLiteral("donk"));
    if (!donkValue.isUndefined()) {
        if (!donkValue.isArray()) {
            addIssue(issues, path + QStringLiteral(".donk"),
                     QStringLiteral("BET_SIZING_TYPE"),
                     QStringLiteral("donk must be an array of numbers"));
        } else {
            QJsonArray normalizedDonk;
            const QJsonArray donk = donkValue.toArray();
            for (int index = 0; index < donk.size(); ++index) {
                const QJsonValue size = donk.at(index);
                if (!size.isDouble() || !isFiniteNumber(size.toDouble())
                    || size.toDouble() < 0.0 || size.toDouble() > 1000.0) {
                    addIssue(issues,
                             QStringLiteral("%1.donk[%2]").arg(path).arg(index),
                             QStringLiteral("BET_SIZING_VALUE"),
                             QStringLiteral("Donk size must be a number between 0 and 1000"));
                    continue;
                }
                normalizedDonk.append(size.toDouble());
            }
            result.insert(QStringLiteral("donk"), normalizedDonk);
        }
    }

    const QJsonValue allin = side.value(QStringLiteral("allin"));
    if (!allin.isBool()) {
        addIssue(issues, path + QStringLiteral(".allin"),
                 QStringLiteral("BET_SIZING_TYPE"),
                 QStringLiteral("allin must be a boolean"));
    } else {
        result.insert(QStringLiteral("allin"), allin.toBool());
    }
    *normalized = result;
    return issues->isEmpty();
}

bool validateStreet(const QJsonObject &street,
                    const QString &path,
                    QJsonObject *normalized,
                    QList<ValidationIssue> *issues)
{
    static const QSet<QString> allowedKeys = {
        QStringLiteral("oop"), QStringLiteral("ip")
    };
    for (const QString &key : street.keys()) {
        if (!allowedKeys.contains(key)) {
            addIssue(issues, path + QLatin1Char('.') + key,
                     QStringLiteral("BET_SIZING_UNKNOWN_FIELD"),
                     QStringLiteral("Unknown street side: %1").arg(key));
        }
    }

    QJsonObject result;
    for (const QString &sideName : {QStringLiteral("oop"), QStringLiteral("ip")}) {
        const QJsonValue sideValue = street.value(sideName);
        if (!sideValue.isObject()) {
            addIssue(issues, path + QLatin1Char('.') + sideName,
                     QStringLiteral("BET_SIZING_TYPE"),
                     QStringLiteral("%1 must be an object").arg(sideName));
            continue;
        }
        QJsonObject sideResult;
        validateSizingSide(sideValue.toObject(),
                           path + QLatin1Char('.') + sideName,
                           &sideResult,
                           issues);
        result.insert(sideName, sideResult);
    }
    *normalized = result;
    return issues->isEmpty();
}

bool validateBetSizing(const QJsonObject &betSizing,
                       QJsonObject *normalized,
                       QList<ValidationIssue> *issues)
{
    static const QSet<QString> allowedKeys = {
        QStringLiteral("flop"), QStringLiteral("turn"), QStringLiteral("river")
    };
    for (const QString &key : betSizing.keys()) {
        if (!allowedKeys.contains(key)) {
            addIssue(issues, QStringLiteral("spot.betSizing.") + key,
                     QStringLiteral("BET_SIZING_UNKNOWN_FIELD"),
                     QStringLiteral("Unknown street: %1").arg(key));
        }
    }

    QJsonObject result;
    for (const QString &streetName : {QStringLiteral("flop"), QStringLiteral("turn"),
                                      QStringLiteral("river")}) {
        const QJsonValue streetValue = betSizing.value(streetName);
        if (!streetValue.isObject()) {
            addIssue(issues, QStringLiteral("spot.betSizing.") + streetName,
                     QStringLiteral("BET_SIZING_TYPE"),
                     QStringLiteral("%1 must be an object").arg(streetName));
            continue;
        }
        QJsonObject streetResult;
        validateStreet(streetValue.toObject(),
                       QStringLiteral("spot.betSizing.") + streetName,
                       &streetResult,
                       issues);
        result.insert(streetName, streetResult);
    }
    *normalized = result;
    return issues->isEmpty();
}

} // namespace

RangeValidationResult SpotValidator::validateRange(const QString &range,
                                                   const QStringList &board,
                                                   const QString &mode)
{
    RangeValidationResult result;
    const QString normalizedMode = mode.trimmed().isEmpty()
                                       ? QStringLiteral("holdem")
                                       : mode.trimmed().toLower();
    if (normalizedMode != QStringLiteral("holdem")
        && normalizedMode != QStringLiteral("shortdeck")) {
        addIssue(&result.errors, QStringLiteral("mode"),
                 QStringLiteral("MODE_UNSUPPORTED"),
                 QStringLiteral("mode must be holdem or shortdeck"));
        return result;
    }

    QStringList normalizedBoard;
    QSet<int> boardCards;
    parseBoard(board,
               normalizedMode == QStringLiteral("shortdeck"),
               &normalizedBoard,
               &boardCards,
               &result.errors,
               QStringLiteral("board"));
    if (!result.errors.isEmpty()) {
        return result;
    }

    validateRangeInternal(range,
                          boardCards,
                          normalizedMode == QStringLiteral("shortdeck"),
                          &result.normalizedRange,
                          &result.comboCount,
                          &result.warnings,
                          &result.errors,
                          QStringLiteral("range"));
    result.valid = result.errors.isEmpty() && result.comboCount > 0;
    return result;
}
namespace {

bool normalizePosition(const QString &raw, QString *normalized)
{
    static const QSet<QString> allowedPositions = {
        QStringLiteral("UTG"),
        QStringLiteral("UTG+1"),
        QStringLiteral("UTG+2"),
        QStringLiteral("MP"),
        QStringLiteral("MP+1"),
        QStringLiteral("HJ"),
        QStringLiteral("CO"),
        QStringLiteral("BTN"),
        QStringLiteral("SB"),
        QStringLiteral("BB"),
        QStringLiteral("OOP"),
        QStringLiteral("IP")
    };
    const QString position = raw.trimmed().toUpper();
    if (!allowedPositions.contains(position)) {
        return false;
    }
    *normalized = position;
    return true;
}

} // namespace

SpotValidationResult SpotValidator::validate(const SpotSpec &spot)
{
    SpotValidationResult result;
    SpotSpec normalized = spot;

    normalized.gameType = spot.gameType.trimmed().toUpper();
    if (normalized.gameType != QStringLiteral("NLHE")) {
        addIssue(&result.errors, QStringLiteral("spot.gameType"),
                 QStringLiteral("GAME_TYPE_UNSUPPORTED"),
                 QStringLiteral("Only NLHE gameType is supported"));
    }

    normalized.mode = spot.mode.trimmed().toLower();
    const bool shortdeck = normalized.mode == QStringLiteral("shortdeck");
    if (normalized.mode != QStringLiteral("holdem") && !shortdeck) {
        addIssue(&result.errors, QStringLiteral("spot.mode"),
                 QStringLiteral("MODE_UNSUPPORTED"),
                 QStringLiteral("mode must be holdem or shortdeck"));
    }

    QStringList normalizedPositions;
    if (spot.positions.size() != 2) {
        addIssue(&result.errors, QStringLiteral("spot.positions"),
                 QStringLiteral("POSITION_COUNT"),
                 QStringLiteral("Exactly two positions are required"));
    } else {
        QSet<QString> seenPositions;
        for (int index = 0; index < spot.positions.size(); ++index) {
            QString position;
            if (!normalizePosition(spot.positions.at(index), &position)) {
                addIssue(&result.errors,
                         QStringLiteral("spot.positions[%1]").arg(index),
                         QStringLiteral("POSITION_INVALID"),
                         QStringLiteral("Unsupported position: %1")
                             .arg(spot.positions.at(index)));
                continue;
            }
            if (seenPositions.contains(position)) {
                addIssue(&result.errors,
                         QStringLiteral("spot.positions[%1]").arg(index),
                         QStringLiteral("POSITION_DUPLICATE"),
                         QStringLiteral("Duplicate position: %1").arg(position));
                continue;
            }
            seenPositions.insert(position);
            normalizedPositions.append(position);
        }
    }
    normalized.positions = normalizedPositions;

    if (!isFiniteNumber(spot.effectiveStackBb) || spot.effectiveStackBb <= 0.0) {
        addIssue(&result.errors, QStringLiteral("spot.effectiveStackBb"),
                 QStringLiteral("STACK_INVALID"),
                 QStringLiteral("effectiveStackBb must be a positive finite number"));
    }

    if (spot.blinds.size() != 2) {
        addIssue(&result.errors, QStringLiteral("spot.blinds"),
                 QStringLiteral("BLINDS_INVALID"),
                 QStringLiteral("blinds must contain exactly two values"));
    } else {
        if (!isFiniteNumber(spot.blinds.at(0)) || spot.blinds.at(0) <= 0.0
            || !isFiniteNumber(spot.blinds.at(1)) || spot.blinds.at(1) <= 0.0) {
            addIssue(&result.errors, QStringLiteral("spot.blinds"),
                     QStringLiteral("BLINDS_INVALID"),
                     QStringLiteral("Small blind and big blind must be positive"));
        } else if (spot.blinds.at(0) > spot.blinds.at(1)) {
            addIssue(&result.errors, QStringLiteral("spot.blinds"),
                     QStringLiteral("BLINDS_ORDER"),
                     QStringLiteral("Small blind must not exceed the big blind"));
        }
    }

    if (!isFiniteNumber(spot.pot) || spot.pot < 0.0) {
        addIssue(&result.errors, QStringLiteral("spot.pot"),
                 QStringLiteral("POT_INVALID"),
                 QStringLiteral("pot must be a non-negative finite number"));
    }

    if (spot.board.size() < 3 || spot.board.size() > 5) {
        addIssue(&result.errors, QStringLiteral("spot.board"),
                 QStringLiteral("BOARD_CARD_COUNT"),
                 QStringLiteral("board must contain 3, 4, or 5 cards"));
    }
    QStringList normalizedBoard;
    QSet<int> boardCards;
    parseBoard(spot.board, shortdeck, &normalizedBoard, &boardCards,
               &result.errors, QStringLiteral("spot.board"));
    normalized.board = normalizedBoard;

    if (!isFiniteNumber(spot.allinThreshold)
        || spot.allinThreshold < 0.0 || spot.allinThreshold > 1.0) {
        addIssue(&result.errors, QStringLiteral("spot.allinThreshold"),
                 QStringLiteral("ALLIN_THRESHOLD_INVALID"),
                 QStringLiteral("allinThreshold must be between 0 and 1"));
    }

    QJsonObject normalizedBetSizing;
    validateBetSizing(spot.betSizing, &normalizedBetSizing, &result.errors);
    normalized.betSizing = normalizedBetSizing;

    QHash<QString, QString> normalizedRanges;
    static const QSet<QString> knownRangeKeys = {
        QStringLiteral("UTG"), QStringLiteral("UTG+1"), QStringLiteral("UTG+2"),
        QStringLiteral("MP"), QStringLiteral("MP+1"), QStringLiteral("HJ"),
        QStringLiteral("CO"), QStringLiteral("BTN"), QStringLiteral("SB"),
        QStringLiteral("BB"), QStringLiteral("OOP"), QStringLiteral("IP")
    };
    const QStringList rangeKeys = spot.ranges.keys();
    for (const QString &rawKey : rangeKeys) {
        const QString key = rawKey.trimmed().toUpper();
        if (!knownRangeKeys.contains(key)) {
            addIssue(&result.errors, QStringLiteral("spot.ranges.") + rawKey,
                     QStringLiteral("RANGE_PLAYER_UNKNOWN"),
                     QStringLiteral("Unknown range player: %1").arg(rawKey));
            continue;
        }
        if (normalizedRanges.contains(key)) {
            addIssue(&result.errors, QStringLiteral("spot.ranges.") + rawKey,
                     QStringLiteral("RANGE_PLAYER_DUPLICATE"),
                     QStringLiteral("Duplicate range player after normalization: %1").arg(key));
            continue;
        }
        const QJsonValue value = spot.ranges.value(rawKey);
        if (!value.isString()) {
            addIssue(&result.errors, QStringLiteral("spot.ranges.") + rawKey,
                     QStringLiteral("RANGE_TYPE"),
                     QStringLiteral("Range value must be a string"));
            continue;
        }
        normalizedRanges.insert(key, value.toString());
    }

    if (normalizedPositions.size() == 2) {
        for (const QString &position : normalizedPositions) {
            if (!normalizedRanges.contains(position)) {
                addIssue(&result.errors, QStringLiteral("spot.ranges.") + position,
                         QStringLiteral("RANGE_PLAYER_MISSING"),
                         QStringLiteral("Missing range for position %1").arg(position));
            }
        }
        if (rangeKeys.size() != 2) {
            addIssue(&result.errors, QStringLiteral("spot.ranges"),
                     QStringLiteral("RANGE_PLAYER_COUNT"),
                     QStringLiteral("Exactly two player ranges are required"));
        }
    }

    QJsonObject normalizedRangesObject;
    for (auto it = normalizedRanges.cbegin(); it != normalizedRanges.cend(); ++it) {
        QString normalizedRange;
        int comboCount = 0;
        validateRangeInternal(it.value(),
                              boardCards,
                              shortdeck,
                              &normalizedRange,
                              &comboCount,
                              &result.warnings,
                              &result.errors,
                              QStringLiteral("spot.ranges.") + it.key());
        normalizedRangesObject.insert(it.key(), normalizedRange);
    }
    normalized.ranges = normalizedRangesObject;

    result.valid = result.errors.isEmpty();
    result.normalizedSpot = normalized;
    return result;
}

} // namespace GtoApi
