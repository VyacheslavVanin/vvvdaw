#pragma once
#include <QString>
#include <QJsonObject>
#include <vector>
#include "plugin/PluginChain.h"

class PluginManager;
class AudioBus;

// True when routing edge from->to (either the main output or a send target)
// would create a cycle in the bus routing graph. `fromIndex`/`toIndex` are bus
// indices; `toIndex < 0` means the output device, which never forms a cycle.
// A cycle exists when `toIndex` can reach `fromIndex` following the existing
// main-output and send edges.
bool wouldCreateBusCycle(const std::vector<AudioBus>& buses,
                         int fromIndex, int toIndex);

class AudioBus {
public:
    AudioBus() = default;

    // One additional split of this bus's signal into another bus. Pre-fader
    // sends are tapped after the plugin chain but before the bus's volume
    // fader; post-fader sends after it. Both are scaled by `level`.
    struct Send {
        int busIndex = 0;
        float level = 1.0f;
        bool preFader = false;

        int bus() const { return busIndex; }
        void setBus(int idx) { busIndex = idx; }
        float levelValue() const { return level; }
        void setLevel(float value) { level = value; }
        bool isPreFader() const { return preFader; }
        void setPreFader(bool pre) { preFader = pre; }
    };

    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    float pan() const { return m_pan; }
    void setPan(float pan) { m_pan = pan; }

    float volume() const { return m_volume; }
    void setVolume(float volume) { m_volume = volume; }

    int outputBusIndex() const { return m_outputBusIndex; }
    void setOutputBusIndex(int idx) { m_outputBusIndex = idx; }

    std::vector<Send>& sends() { return m_sends; }
    const std::vector<Send>& sends() const { return m_sends; }
    void setSends(std::vector<Send> sends) { m_sends = std::move(sends); }

    bool isSolo() const { return m_solo; }
    void setSolo(bool solo) { m_solo = solo; }

    bool isMuted() const { return m_muted; }
    void setMuted(bool muted) { m_muted = muted; }

    bool removable() const { return m_removable; }
    void setRemovable(bool removable) { m_removable = removable; }

    PluginChain& pluginChain() { return m_pluginChain; }
    const PluginChain& pluginChain() const { return m_pluginChain; }

    QJsonObject toJson() const;
    static AudioBus fromJson(const QJsonObject& obj, PluginManager* manager = nullptr);

private:
    QString m_name;
    float m_pan = 0.0f;
    float m_volume = 1.0f;
    int m_outputBusIndex = 0;
    std::vector<Send> m_sends;
    bool m_solo = false;
    bool m_muted = false;
    bool m_removable = true;
    PluginChain m_pluginChain;
};
