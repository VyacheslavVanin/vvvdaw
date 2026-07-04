#pragma once
#include <QString>
#include <QJsonObject>
#include <vector>
#include <memory>
#include "Track.h"
#include "AudioBus.h"

class Project {
public:
    Project();

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

    Track* addTrack(const QString& name = {});
    bool removeTrack(int index);

    int addBus(const AudioBus& bus);
    bool removeBus(int index);

    QString audioDirectory() const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

private:

    QString m_filePath;
    QString m_name;
    int m_sampleRate = 48000;

    std::vector<Track> m_tracks;
    std::vector<AudioBus> m_buses;
};
