#include "AudioBus.h"
#include <QJsonObject>

QJsonObject AudioBus::toJson() const {
    QJsonObject obj;
    obj["name"] = m_name;
    obj["pan"] = m_pan;
    obj["volume"] = m_volume;
    obj["outputBusIndex"] = m_outputBusIndex;
    obj["solo"] = m_solo;
    obj["muted"] = m_muted;
    obj["removable"] = m_removable;
    if (m_pluginChain.count() > 0)
        obj["plugins"] = m_pluginChain.toJson();
    return obj;
}

AudioBus AudioBus::fromJson(const QJsonObject& obj, PluginManager* manager) {
    AudioBus bus;
    bus.setName(obj["name"].toString("Bus"));
    bus.setPan(static_cast<float>(obj["pan"].toDouble(0.0)));
    bus.setVolume(static_cast<float>(obj["volume"].toDouble(1.0)));
    bus.setOutputBusIndex(obj["outputBusIndex"].toInt(0));
    bus.setSolo(obj["solo"].toBool(false));
    bus.setMuted(obj["muted"].toBool(false));
    bus.setRemovable(obj["removable"].toBool(true));
    if (obj.contains("plugins"))
        bus.pluginChain().fromJson(obj["plugins"].toObject(), manager);
    return bus;
}
