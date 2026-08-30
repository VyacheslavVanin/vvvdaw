#pragma once
#include <QString>
#include <QJsonObject>
#include <vector>
#include <cstdint>
#include "AudioEvent.h"
#include "MidiEvent.h"
#include "plugin/PluginChain.h"
#include "core/Constants.h"

class PluginManager;

class Track {
public:
    enum class Type { Audio, Midi };

    Track() = default;
    explicit Track(const QString& name, int channels = 2);
    Track(const QString& name, Type type);

    Type type() const { return m_type; }

    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    int inputDeviceId() const { return m_inputDeviceId; }
    void setInputDeviceId(int id) { m_inputDeviceId = id; }

    int height() const { return m_height; }
    void setHeight(int h) { m_height = h; }

    // Width of the track's effects (plugin list) panel, in pixels.
    int pluginPanelWidth() const { return m_pluginPanelWidth; }
    void setPluginPanelWidth(int w) { m_pluginPanelWidth = w; }

    int channels() const { return m_channels; }
    void setChannels(int ch) { m_channels = ch; }

    int inputChannel() const { return m_inputChannel; }
    void setInputChannel(int ch) { m_inputChannel = ch; }

    int outputBusIndex() const { return m_outputBusIndex; }
    void setOutputBusIndex(int idx) { m_outputBusIndex = idx; }

    int midiOutputDeviceId() const { return m_midiOutputDeviceId; }
    void setMidiOutputDeviceId(int id) { m_midiOutputDeviceId = id; }

    // MIDI output channel (0-15). Used when scheduling notes / control events
    // for this track, instead of deriving a channel from the track index.
    int midiChannel() const { return m_midiChannel; }
    void setMidiChannel(int channel) {
        m_midiChannel = channel < 0 ? 0 : (channel > 15 ? 15 : channel);
    }

    const QString& midiOutputDeviceName() const { return m_midiOutputDeviceName; }
    void setMidiOutputDeviceName(const QString& name) { m_midiOutputDeviceName = name; }

    int instrumentIndex() const { return m_instrumentIndex; }
    void setInstrumentIndex(int idx) { m_instrumentIndex = idx; }

    bool isRecordArmed() const { return m_recordArmed; }
    void setRecordArmed(bool armed) { m_recordArmed = armed; }

    bool isSolo() const { return m_solo; }
    void setSolo(bool solo) { m_solo = solo; }

    bool isMuted() const { return m_muted; }
    void setMuted(bool muted) { m_muted = muted; }

    bool isMonitoring() const { return m_monitoring; }
    void setMonitoring(bool mon) { m_monitoring = mon; }

    float pan() const { return m_pan; }
    void setPan(float pan) { m_pan = pan; }

    float volume() const { return m_volume; }
    void setVolume(float volume) { m_volume = volume; }

    std::vector<AudioEvent>& events() { return m_events; }
    const std::vector<AudioEvent>& events() const { return m_events; }

    std::vector<MidiEvent>& midiEvents() { return m_midiEvents; }
    const std::vector<MidiEvent>& midiEvents() const { return m_midiEvents; }

    PluginChain& pluginChain() { return m_pluginChain; }
    const PluginChain& pluginChain() const { return m_pluginChain; }

    void addEvent(AudioEvent event);
    void importEvent(AudioEvent event);
    void removeEvent(int64_t eventId);
    AudioEvent* findEvent(int64_t eventId);

    void addMidiEvent(MidiEvent event);
    void importMidiEvent(MidiEvent event);
    void removeMidiEvent(int64_t eventId);
    MidiEvent* findMidiEvent(int64_t eventId);

    QJsonObject toJson(const QString& projectDir = {}) const;
    void fromJson(const QJsonObject& obj, const QString& projectDir = {},
                  PluginManager* manager = nullptr);

private:
    Type m_type = Type::Audio;
    QString m_name;
    int m_inputDeviceId = -1;
    int m_inputChannel = 0;
    int m_outputBusIndex = 0;
    int m_channels = 2;
    int m_height = vvvdaw::DefaultTrackHeight;
    int m_pluginPanelWidth = vvvdaw::DefaultPluginPanelWidth;

    int m_midiOutputDeviceId = -1;
    QString m_midiOutputDeviceName;
    int m_midiChannel = 0;
    int m_instrumentIndex = -1;

    bool m_recordArmed = false;
    bool m_solo = false;
    bool m_muted = false;
    bool m_monitoring = false;

    float m_pan = 0.0f;
    float m_volume = static_cast<float>(vvvdaw::DefaultVolume);

    std::vector<AudioEvent> m_events;
    int64_t m_nextEventId = 1;
    std::vector<MidiEvent> m_midiEvents;
    int64_t m_nextMidiEventId = 1;

    PluginChain m_pluginChain;
};
