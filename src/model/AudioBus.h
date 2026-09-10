#pragma once
#include <QString>
#include <QColor>
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

// Where a sidechain send lives: the source bus carrying it and its position in
// that bus's send list. A missing assignment yields sourceBus == -1.
struct SidechainSendLocation {
    int sourceBus = -1;
    int sendIndex = -1;
};

// Finds the sidechain send feeding plugin `pluginId` in `targetBus`'s plugin
// chain. Returns a location with sourceBus == -1 when none exists.
SidechainSendLocation findSidechainSend(const std::vector<AudioBus>& buses,
                                        int targetBus, const QString& pluginId);

// Removes every sidechain send feeding plugin `pluginId` in `targetBus`'s
// chain (used when that plugin is removed).
void removeSidechainSendsForPlugin(std::vector<AudioBus>& buses,
                                   int targetBus, const QString& pluginId);

class AudioBus {
public:
    AudioBus() = default;

    // One additional split of this bus's signal. `Kind::Bus` routes the tap
    // into another bus's mix buffer. `Kind::Sidechain` feeds the tap to the
    // sidechain (key) input of a plugin living in `busIndex`'s plugin chain,
    // identified by `pluginId`. Pre-fader sends are tapped after the plugin
    // chain but before the bus's volume fader; post-fader sends after it. Both
    // are scaled by `level`.
    struct Send {
        enum class Kind { Bus, Sidechain };

        Kind kind = Kind::Bus;
        int busIndex = 0;
        // Plugin target for Kind::Sidechain (resolved by PluginChain::pluginById
        // in the destination bus's chain).
        QString pluginId;
        float level = 1.0f;
        bool preFader = false;

        int bus() const { return busIndex; }
        void setBus(int idx) { busIndex = idx; }
        bool isSidechain() const { return kind == Kind::Sidechain; }
        void setSidechain(bool value) {
            kind = value ? Kind::Sidechain : Kind::Bus;
        }
        const QString& sidechainPluginId() const { return pluginId; }
        void setSidechainPluginId(const QString& id) { pluginId = id; }
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

    // Display state: a bus shown as a folder hides its child buses (buses whose
    // main output routes into it) while collapsed.
    bool folderCollapsed() const { return m_folderCollapsed; }
    void setFolderCollapsed(bool collapsed) { m_folderCollapsed = collapsed; }

    // User-assigned color. When unset (colorSet() == false) the bus falls back
    // to the automatic color (a parent folder's color or a stable per-index
    // tint). Assigning a color to a folder propagates it down to child buses
    // that have not had a color manually assigned themselves.
    bool colorSet() const { return m_colorSet; }
    QColor color() const { return m_color; }
    void setColor(const QColor& color) { m_color = color; m_colorSet = true; }
    void clearColor() { m_colorSet = false; m_color = QColor(); }

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
    bool m_folderCollapsed = false;
    bool m_colorSet = false;
    QColor m_color;
    PluginChain m_pluginChain;
};
