#include "Instrument.h"
#include "plugin/PluginChain.h"
#include <QJsonObject>

QJsonObject Instrument::toJson() const {
    QJsonObject obj;
    obj["name"] = m_name;
    obj["pan"] = m_pan;
    obj["volume"] = m_volume;
    obj["outputBusIndex"] = m_outputBusIndex;
    obj["solo"] = m_solo;
    obj["muted"] = m_muted;
    if (m_synth)
        obj["synth"] = m_synth->stateToJson();
    if (m_effects.count() > 0)
        obj["effects"] = m_effects.toJson();
    return obj;
}

Instrument Instrument::fromJson(const QJsonObject& obj, PluginManager* manager) {
    Instrument instrument;
    instrument.setName(obj["name"].toString("Instrument"));
    instrument.setPan(static_cast<float>(obj["pan"].toDouble(0.0)));
    instrument.setVolume(static_cast<float>(obj["volume"].toDouble(1.0)));
    instrument.setOutputBusIndex(obj["outputBusIndex"].toInt(0));
    instrument.setSolo(obj["solo"].toBool(false));
    instrument.setMuted(obj["muted"].toBool(false));
    if (obj.contains("synth")) {
        auto synth = PluginChain::createInstance(obj["synth"].toObject(), manager);
        if (synth)
            instrument.setSynth(std::move(synth));
    }
    if (obj.contains("effects"))
        instrument.effects().fromJson(obj["effects"].toObject(), manager);
    return instrument;
}
