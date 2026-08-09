#include "Instrument.h"
#include "plugin/PluginChain.h"
#include <QJsonObject>
#include <QJsonArray>

QJsonObject Instrument::routingToJson() const {
    QJsonObject obj;
    obj["multiChannel"] = m_multiChannel;
    QJsonArray routes;
    for (const auto& route : m_channelRoutes) {
        QJsonObject r;
        r["bus"] = route.busIndex;
        r["name"] = route.name;
        routes.append(r);
    }
    obj["channelRoutes"] = routes;
    return obj;
}

void Instrument::applyRoutingFromJson(const QJsonObject& obj) {
    m_multiChannel = obj["multiChannel"].toBool(false);
    m_channelRoutes.clear();
    QJsonArray routes = obj["channelRoutes"].toArray();
    for (auto v : routes) {
        QJsonObject r = v.toObject();
        ChannelRoute route;
        route.busIndex = r["bus"].toInt(0);
        route.name = r["name"].toString();
        m_channelRoutes.push_back(std::move(route));
    }
}

QJsonObject Instrument::toJson() const {
    QJsonObject obj;
    obj["name"] = m_name;
    obj["pan"] = m_pan;
    obj["volume"] = m_volume;
    obj["outputBusIndex"] = m_outputBusIndex;
    obj["solo"] = m_solo;
    obj["muted"] = m_muted;
    obj["routing"] = routingToJson();
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
    if (obj.contains("routing"))
        instrument.applyRoutingFromJson(obj["routing"].toObject());
    if (obj.contains("synth")) {
        auto synth = PluginChain::createInstance(obj["synth"].toObject(), manager);
        if (synth)
            instrument.setSynth(std::move(synth));
    }
    if (obj.contains("effects"))
        instrument.effects().fromJson(obj["effects"].toObject(), manager);
    return instrument;
}
