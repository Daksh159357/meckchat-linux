#include "meckchat/core/config.h"
#include <QUuid>

namespace MeckChat::Core {

AppConfig& AppConfig::instance() {
    static AppConfig s_instance;
    return s_instance;
}

AppConfig::AppConfig() {
    load();
}

void AppConfig::load() {
    QSettings settings("MeckChat", "MeckChatLinux");
    m_deviceId = settings.value("deviceId", "").toString();
    if (m_deviceId.isEmpty()) {
        m_deviceId = "mc_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue("deviceId", m_deviceId);
    }
    m_displayName = settings.value("displayName", "Linux Device").toString();
    m_virtualIp = settings.value("virtualIp", "10.77.0.2").toString();
}

void AppConfig::save() {
    QSettings settings("MeckChat", "MeckChatLinux");
    settings.setValue("deviceId", m_deviceId);
    settings.setValue("displayName", m_displayName);
    settings.setValue("virtualIp", m_virtualIp);
}

QString AppConfig::deviceId() const { return m_deviceId; }
void AppConfig::setDeviceId(const QString &id) { m_deviceId = id; save(); }

QString AppConfig::displayName() const { return m_displayName; }
void AppConfig::setDisplayName(const QString &name) { m_displayName = name; save(); }

QString AppConfig::platform() const { return m_platform; }
QString AppConfig::mqttBrokerHost() const { return m_mqttBrokerHost; }
int AppConfig::mqttBrokerPort() const { return m_mqttBrokerPort; }

QString AppConfig::virtualIp() const { return m_virtualIp; }
void AppConfig::setVirtualIp(const QString &ip) { m_virtualIp = ip; save(); }

} // namespace MeckChat::Core
