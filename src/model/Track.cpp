#include "Track.h"

Track::Track(const QString& name, int channels)
    : m_type(Type::Audio), m_name(name), m_channels(channels)
{
}

Track::Track(const QString& name, Type type)
    : m_type(type), m_name(name)
{
}

Track::Track(Type type, const QString& name)
    : m_type(type), m_name(name)
{
}

void Track::addEvent(AudioEvent event) {
    event.setId(m_nextEventId++);
    m_events.push_back(std::move(event));
}

void Track::importEvent(AudioEvent event) {
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
