#include "Track.h"
#include "core/Constants.h"
#include <QJsonArray>

Track::Track(const QString& name, int channels)
    : m_type(Type::Audio), m_name(name), m_channels(channels)
{
}

Track::Track(const QString& name, Type type)
    : m_type(type), m_name(name)
{
}

void Track::addEvent(AudioEvent event) {
    event.setId(m_nextEventId++);
    m_events.push_back(std::move(event));
}

void Track::importEvent(AudioEvent event) {
    if (findEvent(event.id()) != nullptr) {
        event.setId(m_nextEventId++);
    } else if (event.id() >= m_nextEventId) {
        m_nextEventId = event.id() + 1;
    }
    m_events.push_back(std::move(event));
}

void Track::removeEvent(int64_t eventId) {
    auto it = std::remove_if(m_events.begin(), m_events.end(),
        [eventId](const AudioEvent& e) { return e.id() == eventId; });
    m_events.erase(it, m_events.end());
}

AudioEvent* Track::findEvent(int64_t eventId) {
    for (auto& e : m_events)
        if (e.id() == eventId) return &e;
    return nullptr;
}

void Track::addMidiEvent(MidiEvent event) {
    event.setId(m_nextMidiEventId++);
    m_midiEvents.push_back(std::move(event));
}

void Track::importMidiEvent(MidiEvent event) {
    if (findMidiEvent(event.id()) != nullptr) {
        event.setId(m_nextMidiEventId++);
    } else if (event.id() >= m_nextMidiEventId) {
        m_nextMidiEventId = event.id() + 1;
    }
    m_midiEvents.push_back(std::move(event));
}

void Track::removeMidiEvent(int64_t eventId) {
    auto it = std::remove_if(m_midiEvents.begin(), m_midiEvents.end(),
        [eventId](const MidiEvent& e) { return e.id() == eventId; });
    m_midiEvents.erase(it, m_midiEvents.end());
}

MidiEvent* Track::findMidiEvent(int64_t eventId) {
    for (auto& e : m_midiEvents)
        if (e.id() == eventId) return &e;
    return nullptr;
}

QJsonObject Track::toJson(const QString& projectDir) const {
    QJsonObject tObj;
    tObj["name"] = m_name;
    tObj["type"] = m_type == Type::Midi ? "midi" : "audio";
    tObj["channels"] = m_channels;
    tObj["inputDeviceId"] = m_inputDeviceId;
    tObj["inputChannel"] = m_inputChannel;
    tObj["outputBusIndex"] = m_outputBusIndex;
    tObj["pan"] = m_pan;
    tObj["volume"] = m_volume;
    tObj["muted"] = m_muted;
    tObj["solo"] = m_solo;

    if (m_type == Type::Midi) {
        tObj["midiOutputDeviceId"] = m_midiOutputDeviceId;
        if (!m_midiOutputDeviceName.isEmpty())
            tObj["midiOutputDeviceName"] = m_midiOutputDeviceName;
        tObj["instrumentIndex"] = m_instrumentIndex;
    }

    QJsonArray eventsArr;
    for (const auto& event : m_events)
        eventsArr.append(event.toJson(projectDir));
    tObj["events"] = eventsArr;

    if (m_type == Type::Midi) {
        QJsonArray midiEventsArr;
        for (const auto& event : m_midiEvents)
            midiEventsArr.append(event.toJson());
        tObj["midiEvents"] = midiEventsArr;
    }

    if (m_pluginChain.count() > 0)
        tObj["plugins"] = m_pluginChain.toJson();
    return tObj;
}

void Track::fromJson(const QJsonObject& tObj, const QString& projectDir, PluginManager* manager) {
    m_type = tObj["type"].toString() == "midi" ? Type::Midi : Type::Audio;
    m_name = tObj["name"].toString();
    m_channels = tObj["channels"].toInt(2);
    m_inputDeviceId = tObj["inputDeviceId"].toInt(-1);
    m_inputChannel = tObj["inputChannel"].toInt(0);
    m_outputBusIndex = tObj["outputBusIndex"].toInt(0);
    m_pan = static_cast<float>(tObj["pan"].toDouble(0.0));
    m_volume = static_cast<float>(tObj["volume"].toDouble(vvvdaw::DefaultVolume));
    m_muted = tObj["muted"].toBool(false);
    m_solo = tObj["solo"].toBool(false);

    if (m_type == Type::Midi) {
        m_midiOutputDeviceId = tObj["midiOutputDeviceId"].toInt(-1);
        m_midiOutputDeviceName = tObj["midiOutputDeviceName"].toString();
        m_instrumentIndex = tObj["instrumentIndex"].toInt(-1);
    }

    m_events.clear();
    const QJsonArray eventsArr = tObj["events"].toArray();
    for (const auto& eVal : eventsArr)
        addEvent(AudioEvent::fromJson(eVal.toObject(), projectDir));

    if (m_type == Type::Midi) {
        m_midiEvents.clear();
        const QJsonArray midiEventsArr = tObj["midiEvents"].toArray();
        for (const auto& eVal : midiEventsArr)
            addMidiEvent(MidiEvent::fromJson(eVal.toObject()));
    }

    if (tObj.contains("plugins"))
        m_pluginChain.fromJson(tObj["plugins"].toObject(), manager);
}
