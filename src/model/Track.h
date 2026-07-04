#pragma once
#include <QString>
#include <vector>
#include <cstdint>
#include "AudioEvent.h"

class Track {
public:
    Track() = default;
    explicit Track(const QString& name);

    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    int inputDeviceId() const { return m_inputDeviceId; }
    void setInputDeviceId(int id) { m_inputDeviceId = id; }

    int inputChannel() const { return m_inputChannel; }
    void setInputChannel(int ch) { m_inputChannel = ch; }

    int outputBusIndex() const { return m_outputBusIndex; }
    void setOutputBusIndex(int idx) { m_outputBusIndex = idx; }

    bool isRecordArmed() const { return m_recordArmed; }
    void setRecordArmed(bool armed) { m_recordArmed = armed; }

    bool isSolo() const { return m_solo; }
    void setSolo(bool solo) { m_solo = solo; }

    bool isMuted() const { return m_muted; }
    void setMuted(bool muted) { m_muted = muted; }

    float pan() const { return m_pan; }
    void setPan(float pan) { m_pan = pan; }

    float volume() const { return m_volume; }
    void setVolume(float volume) { m_volume = volume; }

    std::vector<AudioEvent>& events() { return m_events; }
    const std::vector<AudioEvent>& events() const { return m_events; }

    void addEvent(const AudioEvent& event);
    void removeEvent(int64_t eventId);
    AudioEvent* findEvent(int64_t eventId);

    uint32_t atomicState() const;

private:
    QString m_name;
    int m_inputDeviceId = -1;
    int m_inputChannel = 0;
    int m_outputBusIndex = 0;

    bool m_recordArmed = false;
    bool m_solo = false;
    bool m_muted = false;

    float m_pan = 0.0f;
    float m_volume = 0.8f;

    std::vector<AudioEvent> m_events;
    int64_t m_nextEventId = 1;
};
