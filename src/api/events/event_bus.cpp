#include "event_bus.h"

namespace GtoApi {

EventBus::EventBus(QObject *parent)
    : QObject(parent)
{
}

void EventBus::publish(const QString &method, QJsonObject params)
{
    ++m_sequence;
    params.insert(QStringLiteral("sequence"), static_cast<double>(m_sequence));
    emit eventReady(method, params);
}

} // namespace GtoApi