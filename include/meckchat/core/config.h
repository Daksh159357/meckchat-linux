#pragma once

#include <QString>
#include <QSettings>

namespace MeckChat::Core {

class AppConfig {
public:
    static AppConfig& instance();

    void load();
    void save();

    QString deviceId() const;
    void setDeviceId(const QString &id);

    QString displayName() const;
    void setDisplayName(const QString &name);

    QString platform() const;

    QString mqttBrokerHost() const;
    int mqttBrokerPort() const;

    QString virtualIp() const;
    void setVirtualIp(const QString &ip);

private:
    AppConfig();
    QString m_deviceId;
    QString m_displayName;
    QString m_platform{"linux"};
    QString m_mqttBrokerHost{"broker.hivemq.com"};
    int m_mqttBrokerPort{8883};
    QString m_virtualIp{"10.77.0.2"};
};

} // namespace MeckChat::Core
