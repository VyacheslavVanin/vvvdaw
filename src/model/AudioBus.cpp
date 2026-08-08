#include "AudioBus.h"
#include <QJsonObject>

QJsonObject AudioBus::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["pan"] = pan;
    obj["volume"] = volume;
    obj["outputBusIndex"] = outputBusIndex;
    obj["solo"] = solo;
    obj["muted"] = muted;
    obj["removable"] = removable;
    if (pluginChain.count() > 0)
        obj["plugins"] = pluginChain.toJson();
    return obj;
}

AudioBus AudioBus::fromJson(const QJsonObject& obj, PluginManager* manager) {
    AudioBus bus;
    bus.name = obj["name"].toString("Bus");
    bus.pan = static_cast<float>(obj["pan"].toDouble(0.0));
    bus.volume = static_cast<float>(obj["volume"].toDouble(1.0));
    bus.outputBusIndex = obj["outputBusIndex"].toInt(0);
    bus.solo = obj["solo"].toBool(false);
    bus.muted = obj["muted"].toBool(false);
    bus.removable = obj["removable"].toBool(true);
    if (obj.contains("plugins"))
        bus.pluginChain.fromJson(obj["plugins"].toObject(), manager);
    return bus;
}
