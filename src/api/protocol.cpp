#include "protocol.h"

namespace GtoApi {

QJsonObject makeSuccess(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    if (!id.isUndefined()) {
        response.insert(QStringLiteral("id"), id);
    }
    response.insert(QStringLiteral("result"), result);
    return response;
}

QJsonObject makeFailure(const QJsonValue &id,
                        int code,
                        const QString &message,
                        const QJsonValue &data)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);
    if (!data.isUndefined()) {
        error.insert(QStringLiteral("data"), data);
    }

    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id.isUndefined() ? QJsonValue::Null : id);
    response.insert(QStringLiteral("error"), error);
    return response;
}

QJsonObject errorData(const QString &code, const QJsonObject &details)
{
    QJsonObject data = details;
    data.insert(QStringLiteral("code"), code);
    return data;
}

} // namespace GtoApi