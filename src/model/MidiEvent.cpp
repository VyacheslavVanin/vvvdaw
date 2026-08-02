#include "MidiEvent.h"

int64_t MidiEvent::endSample() const {
    return m_startSample + m_durationSample;
}

void MidiEvent::addTake(std::shared_ptr<MidiClip> takeClip) {
    m_takes.push_back(takeClip);
    m_activeTakeIndex = static_cast<int>(m_takes.size()) - 1;
    m_clip = takeClip;
}

void MidiEvent::setActiveTake(int index) {
    if (index >= 0 && index < static_cast<int>(m_takes.size())) {
        m_activeTakeIndex = index;
        m_clip = m_takes[index];
    }
}

const std::shared_ptr<MidiClip>& MidiEvent::activeClip() const {
    if (m_activeTakeIndex >= 0 && m_activeTakeIndex < static_cast<int>(m_takes.size()))
        return m_takes[m_activeTakeIndex];
    return m_clip;
}
