#include "Track.h"

Track::Track(const QString& name)
    : m_name(name)
{
}

void Track::addEvent(const AudioEvent& event) {
    AudioEvent ev = event;
    ev.id = m_nextEventId++;
    m_events.push_back(ev);
}

void Track::importEvent(const AudioEvent& event) {
    m_events.push_back(event);
}

void Track::removeEvent(int64_t eventId) {
    auto it = std::remove_if(m_events.begin(), m_events.end(),
        [eventId](const AudioEvent& e) { return e.id == eventId; });
    m_events.erase(it, m_events.end());
}

AudioEvent* Track::findEvent(int64_t eventId) {
    for (auto& e : m_events)
        if (e.id == eventId) return &e;
    return nullptr;
}

uint32_t Track::atomicState() const {
    uint32_t state = 0;
    if (m_muted)        state |= (1u << 0);
    if (m_solo)         state |= (1u << 1);
    if (m_recordArmed)  state |= (1u << 2);
    if (m_monitoring)   state |= (1u << 3);
    uint32_t panBits = static_cast<uint32_t>((m_pan * 0.5f + 0.5f) * 255.0f) & 0xFF;
    uint32_t volBits = static_cast<uint32_t>(m_volume * 255.0f) & 0xFF;
    state |= (panBits << 8);
    state |= (volBits << 16);
    return state;
}
