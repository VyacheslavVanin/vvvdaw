#pragma once
#include <QString>
#include <QJsonObject>
#include <vector>
#include <memory>
#include <shared_mutex>
#include <mutex>
#include "Track.h"
#include "AudioBus.h"

class Project {
public:
    Project();

    static constexpr int MetronomeBusIndex = 1;

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

    AudioBus& masterBus() { return m_buses[0]; }
    const AudioBus& masterBus() const { return m_buses[0]; }

    Track* addTrack(const QString& name = {});
    bool removeTrack(int index);

    int addBus(const AudioBus& bus);
    bool removeBus(int index);

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
    int m_sampleRate = 48000;
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
};
