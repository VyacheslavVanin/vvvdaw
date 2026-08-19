#pragma once
#include <QString>
#include <QColor>
#include <QJsonObject>
#include <vector>
#include <memory>
#include <shared_mutex>
#include <mutex>
#include "Track.h"
#include "AudioBus.h"
#include "Instrument.h"
#include "MidiClip.h"
#include "core/Constants.h"

class PluginManager;

class Project {
public:
    Project();

    static constexpr int MetronomeBusIndex = 1;

    void setPluginManager(PluginManager* pm) { m_pluginManager = pm; }

    bool load(const QString& filePath);
    bool save(const QString& filePath);

    const QString& filePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }

    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    int sampleRate() const { return m_sampleRate; }
    void setSampleRate(int rate) { m_sampleRate = rate; }

    std::vector<Track>& tracks() { return m_tracks; }
    const std::vector<Track>& tracks() const { return m_tracks; }

    std::vector<AudioBus>& buses() { return m_buses; }
    const std::vector<AudioBus>& buses() const { return m_buses; }

    std::vector<Instrument>& instruments() { return m_instruments; }
    const std::vector<Instrument>& instruments() const { return m_instruments; }

    // Bounds-checked accessors (return nullptr for out-of-range indices).
    Track* trackAt(int index);
    const Track* trackAt(int index) const;
    AudioBus* busAt(int index);
    const AudioBus* busAt(int index) const;
    Instrument* instrumentAt(int index);
    const Instrument* instrumentAt(int index) const;

    AudioBus& masterBus() { return m_buses[0]; }
    const AudioBus& masterBus() const { return m_buses[0]; }

    Track* addTrack(const QString& name = {}, int channels = 2);
    Track* addMidiTrack(const QString& name = {});
    bool removeTrack(int index);

    int addBus(AudioBus bus);
    bool removeBus(int index);

    // Bus panel display order (indices into buses()). Dragging buses reorders
    // this list instead of the buses vector, so index-addressed references
    // (tracks, instruments, routes, sends) stay stable.
    const std::vector<int>& busDisplayOrder() const { return m_busDisplayOrder; }
    std::vector<int>& busDisplayOrder() { return m_busDisplayOrder; }
    void setBusDisplayOrder(std::vector<int> order) { m_busDisplayOrder = std::move(order); }

    // True when at least one bus routes its main output into `index`.
    bool isBusFolder(int index) const;
    // Buses routed into `index` (its folder children), ordered for display.
    std::vector<int> folderChildren(int index) const;
    // Every recursive child of `index` (children, grandchildren, ...), in
    // display order.
    std::vector<int> folderDescendants(int index) const;
    // Master plus every bus routed to the master bus or the output device, in
    // display order: the top-level sequence of the bus panel.
    std::vector<int> topLevelBusIndices() const;

    // Effective display color of a bus: its own manually assigned color when
    // set; otherwise the nearest ancestor (folder) color propagated down the
    // routing tree; otherwise the automatic stable tint.
    QColor busColor(int busIndex) const;
    // Stable per-folder tint, derived from the folder's bus index.
    static QColor folderColorFor(int folderIndex);

    int addInstrument(Instrument instrument);
    bool removeInstrument(int index);

    QString audioDirectory() const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

    bool snapToGrid() const { return m_snapToGrid; }
    void setSnapToGrid(bool snap) { m_snapToGrid = snap; }

    bool metronomeEnabled() const { return m_metronomeEnabled; }
    void setMetronomeEnabled(bool enabled) { m_metronomeEnabled = enabled; }

    bool precountEnabled() const { return m_precountEnabled; }
    void setPrecountEnabled(bool enabled) { m_precountEnabled = enabled; }

    double tempo() const { return m_tempo; }
    void setTempo(double bpm) { m_tempo = bpm; }

    int timeSigNum() const { return m_timeSigNum; }
    int timeSigDen() const { return m_timeSigDen; }
    void setTimeSignature(int num, int den) { m_timeSigNum = num; m_timeSigDen = den; }

    double samplesPerBeat() const { return (60.0 / m_tempo) * m_sampleRate; }
    double samplesPerBar() const { return samplesPerBeat() * m_timeSigNum; }
    int64_t snapSample(int64_t sample, int beatDivision = 4) const;

    double samplesPerTick() const { return (60.0 / m_tempo) * m_sampleRate / MidiClip::kPPQ; }
    int64_t ticksToSamples(int64_t ticks) const {
        return static_cast<int64_t>(static_cast<double>(ticks) * samplesPerTick());
    }
    int64_t samplesToTicks(int64_t samples) const {
        double ticks = static_cast<double>(samples) / samplesPerTick();
        return static_cast<int64_t>(std::llround(ticks));
    }

    void rescaleTimeline(double factor);

    int64_t loopStart() const { return m_loopStart; }
    int64_t loopEnd() const { return m_loopEnd; }
    void setLoop(int64_t start, int64_t end) { m_loopStart = start; m_loopEnd = end; }
    void clearLoop() { m_loopStart = -1; m_loopEnd = -1; }
    bool hasLoop() const { return m_loopStart >= 0 && m_loopEnd > m_loopStart; }

    int64_t recordRegionStart() const { return m_recordRegionStart; }
    int64_t recordRegionEnd() const { return m_recordRegionEnd; }
    void setRecordRegion(int64_t start, int64_t end) { m_recordRegionStart = start; m_recordRegionEnd = end; }
    void clearRecordRegion() { m_recordRegionStart = -1; m_recordRegionEnd = -1; }
    bool hasRecordRegion() const { return m_recordRegionStart >= 0 && m_recordRegionEnd > m_recordRegionStart; }

    auto readLock() const { return std::shared_lock(*m_mutex); }
    auto writeLock() { return std::unique_lock(*m_mutex); }
    std::shared_mutex& mutex() const { return *m_mutex; }

private:

    mutable std::unique_ptr<std::shared_mutex> m_mutex{std::make_unique<std::shared_mutex>()};

    QString m_filePath;
    QString m_name;
    int m_sampleRate = vvvdaw::DefaultSampleRate;
    double m_tempo = 120.0;
    int m_timeSigNum = 4;
    int m_timeSigDen = 4;
    bool m_snapToGrid = true;
    bool m_metronomeEnabled = false;
    bool m_precountEnabled = false;
    int64_t m_loopStart = -1;
    int64_t m_loopEnd = -1;
    int64_t m_recordRegionStart = -1;
    int64_t m_recordRegionEnd = -1;

    std::vector<Track> m_tracks;
    std::vector<AudioBus> m_buses;
    std::vector<Instrument> m_instruments;
    PluginManager* m_pluginManager = nullptr;
    std::vector<int> m_busDisplayOrder;
};
