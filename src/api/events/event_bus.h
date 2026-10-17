#ifndef GTO_API_EVENT_BUS_H
#define GTO_API_EVENT_BUS_H

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace GtoApi {

class EventBus : public QObject
{
    Q_OBJECT

public:
    explicit EventBus(QObject *parent = nullptr);

    void publish(const QString &method, QJsonObject params);

signals:
    void eventReady(const QString &method, const QJsonObject &params);

private:
    quint64 m_sequence = 0;
};

} // namespace GtoApi

#endif // GTO_API_EVENT_BUS_H