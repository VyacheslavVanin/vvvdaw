#include "Track.h"

Track::Track(const QString& name)
    : m_name(name)
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
