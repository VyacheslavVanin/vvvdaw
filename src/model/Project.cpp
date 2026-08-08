#include "Project.h"
#include "AudioClip.h"
#include "JsonUtils.h"
#include "core/Constants.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <cmath>

namespace {

AudioBus makeDefaultMasterBus() {
    AudioBus master;
    master.name = "Master";
    master.volume = 1.0f;
    master.pan = 0.0f;
    master.outputBusIndex = -1;
    master.removable = false;
    return master;
}

AudioBus makeDefaultMetronomeBus() {
    AudioBus metro;
    metro.name = "Metronome";
    metro.volume = static_cast<float>(vvvdaw::DefaultVolume);
    metro.pan = 0.0f;
    metro.outputBusIndex = 0;
    metro.removable = false;
    return metro;
}

// After removing bus `removed`, remap a stored bus index that pointed at or
// past it: the removed bus becomes the master (0), others shift down.
void remapBusIndexAfterRemoval(int& index, int removed) {
    if (index == removed)
        index = 0;
    else if (index > removed)
        --index;
}

} // namespace

Project::Project()
    : m_name("Untitled")
{
    m_buses.push_back(makeDefaultMasterBus());
    m_buses.push_back(makeDefaultMetronomeBus());
}

bool Project::load(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;

    m_filePath = filePath;
    fromJson(doc.object());
    return true;
}

bool Project::save(const QString& filePath) {
    QFileInfo fi(filePath);
    QString projectDir = fi.absolutePath();
    QString audioDir = projectDir + "/audio";
    QDir().mkpath(audioDir);

    QSet<const AudioClip*> processedClips;
    auto saveClip = [&](const std::shared_ptr<AudioClip>& clip) {
        if (!clip || processedClips.contains(clip.get()))
            return;
        processedClips.insert(clip.get());

        QString srcPath = clip->filePath();
        QString targetPath;
        if (srcPath.isEmpty()) {
            QString name = QString("clip_%1.wav").arg(
                QString::number(reinterpret_cast<quintptr>(clip.get()), 16));
            targetPath = audioDir + "/" + name;
            clip->saveToFile(targetPath);
        } else {
            QFileInfo srcInfo(srcPath);
            QString srcAbs = srcInfo.absoluteFilePath();
            QString audioAbs = QDir(audioDir).absolutePath();

            if (srcInfo.absolutePath() == audioAbs) {
                targetPath = srcAbs;
            } else {
                QString baseName = srcInfo.completeBaseName();
                QString ext = srcInfo.suffix();
                if (ext.isEmpty()) ext = "wav";
                targetPath = audioDir + "/" + baseName + "." + ext;

                int counter = 1;
                while (QFile::exists(targetPath)
                       && QFileInfo(targetPath).absoluteFilePath() != srcAbs) {
                    targetPath = audioDir + "/" + baseName + "_" + QString::number(counter++) + "." + ext;
                }

                if (QFileInfo(targetPath).absoluteFilePath() != srcAbs) {
                    if (!QFile::copy(srcPath, targetPath)) {
                        qWarning() << "Failed to copy audio file:" << srcPath << "->" << targetPath;
                        return;
                    }
                }
            }
        }

        clip->setFilePath(targetPath);
    };

    for (auto& track : m_tracks) {
        for (auto& event : track.events()) {
            saveClip(event.clip());
            for (auto& take : event.takes())
                saveClip(take);
        }
    }

    m_filePath = filePath;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QJsonDocument doc(toJson());
    file.write(doc.toJson());
    return true;
}

Track* Project::addTrack(const QString& name, int channels) {
    Track track(name.isEmpty() ? QString("Track %1").arg(m_tracks.size() + 1) : name, channels);
    m_tracks.push_back(std::move(track));
    return &m_tracks.back();
}

Track* Project::addMidiTrack(const QString& name) {
    Track track(name.isEmpty() ? QString("Track %1").arg(m_tracks.size() + 1) : name, Track::Type::Midi);
    m_tracks.push_back(std::move(track));
    return &m_tracks.back();
}

bool Project::removeTrack(int index) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size()))
        return false;
    m_tracks.erase(m_tracks.begin() + index);
    return true;
}

int Project::addBus(AudioBus bus) {
    m_buses.push_back(std::move(bus));
    return static_cast<int>(m_buses.size()) - 1;
}

int Project::addInstrument(Instrument instrument) {
    m_instruments.push_back(std::move(instrument));
    return static_cast<int>(m_instruments.size()) - 1;
}

bool Project::removeInstrument(int index) {
    if (index < 0 || index >= static_cast<int>(m_instruments.size()))
        return false;

    m_instruments.erase(m_instruments.begin() + index);

    for (auto& track : m_tracks) {
        if (track.instrumentIndex() == index)
            track.setInstrumentIndex(-1);
        else if (track.instrumentIndex() > index)
            track.setInstrumentIndex(track.instrumentIndex() - 1);
    }

    return true;
}

bool Project::removeBus(int index) {
    if (index <= 0 || index >= static_cast<int>(m_buses.size()))
        return false;
    if (!m_buses[index].removable)
        return false;

    m_buses.erase(m_buses.begin() + index);

    for (auto& track : m_tracks) {
        int busIdx = track.outputBusIndex();
        remapBusIndexAfterRemoval(busIdx, index);
        track.setOutputBusIndex(busIdx);
    }

    for (auto& bus : m_buses)
        remapBusIndexAfterRemoval(bus.outputBusIndex, index);

    for (auto& instrument : m_instruments) {
        int busIdx = instrument.outputBusIndex();
        remapBusIndexAfterRemoval(busIdx, index);
        instrument.setOutputBusIndex(busIdx);
    }

    return true;
}

QString Project::audioDirectory() const {
    if (m_filePath.isEmpty()) {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        return dir + "/vvvdaw";
    }
    QFileInfo fi(m_filePath);
    return fi.absolutePath() + "/audio";
}

void Project::rescaleTimeline(double factor) {
    if (factor <= 0.0 || factor == 1.0)
        return;
    if (hasLoop()) {
        m_loopStart = static_cast<int64_t>(std::llround(static_cast<double>(m_loopStart) * factor));
        m_loopEnd = static_cast<int64_t>(std::llround(static_cast<double>(m_loopEnd) * factor));
    }
    if (hasRecordRegion()) {
        m_recordRegionStart = static_cast<int64_t>(std::llround(static_cast<double>(m_recordRegionStart) * factor));
        m_recordRegionEnd = static_cast<int64_t>(std::llround(static_cast<double>(m_recordRegionEnd) * factor));
    }
    for (auto& track : m_tracks) {
        for (auto& event : track.events()) {
            event.setStartSample(static_cast<int64_t>(
                std::llround(static_cast<double>(event.startSample()) * factor)));
            event.setDurationSample(static_cast<int64_t>(
                std::llround(static_cast<double>(event.durationSample()) * factor)));
        }
        for (auto& event : track.midiEvents()) {
            event.setStartSample(static_cast<int64_t>(
                std::llround(static_cast<double>(event.startSample()) * factor)));
            event.setDurationSample(static_cast<int64_t>(
                std::llround(static_cast<double>(event.durationSample()) * factor)));
        }
    }
}

QJsonObject Project::toJson() const {
    QJsonObject obj;
    obj["formatVersion"] = 4;
    obj["name"] = m_name;
    obj["snapToGrid"] = m_snapToGrid;
    obj["metronomeEnabled"] = m_metronomeEnabled;
    obj["precountEnabled"] = m_precountEnabled;
    obj["tempo"] = m_tempo;
    obj["timeSigNum"] = m_timeSigNum;
    obj["timeSigDen"] = m_timeSigDen;
    if (hasLoop()) {
        obj["loopStart"] = static_cast<qint64>(m_loopStart);
        obj["loopEnd"] = static_cast<qint64>(m_loopEnd);
    }
    if (hasRecordRegion()) {
        obj["recordRegionStart"] = static_cast<qint64>(m_recordRegionStart);
        obj["recordRegionEnd"] = static_cast<qint64>(m_recordRegionEnd);
    }

    QString projDir = m_filePath.isEmpty() ? QString()
                     : QFileInfo(m_filePath).absolutePath();

    QJsonArray tracksArr;
    for (const auto& track : m_tracks)
        tracksArr.append(track.toJson(projDir));
    obj["tracks"] = tracksArr;

    QJsonArray busesArr;
    for (const auto& bus : m_buses)
        busesArr.append(bus.toJson());
    obj["buses"] = busesArr;

    QJsonArray instrumentsArr;
    for (const auto& instrument : m_instruments)
        instrumentsArr.append(instrument.toJson());
    obj["instruments"] = instrumentsArr;

    return obj;
}

void Project::fromJson(const QJsonObject& obj) {
    m_name = obj["name"].toString("Untitled");
    m_snapToGrid = obj["snapToGrid"].toBool(true);
    m_metronomeEnabled = obj["metronomeEnabled"].toBool(false);
    m_precountEnabled = obj["precountEnabled"].toBool(false);
    m_tempo = obj["tempo"].toDouble(120.0);
    m_timeSigNum = obj["timeSigNum"].toInt(4);
    m_timeSigDen = obj["timeSigDen"].toInt(4);

    if (obj.contains("loopStart") && obj.contains("loopEnd")) {
        m_loopStart = jsonInt64(obj, "loopStart");
        m_loopEnd = jsonInt64(obj, "loopEnd");
    }
    if (obj.contains("recordRegionStart") && obj.contains("recordRegionEnd")) {
        m_recordRegionStart = jsonInt64(obj, "recordRegionStart");
        m_recordRegionEnd = jsonInt64(obj, "recordRegionEnd");
    }

    QString projDir = m_filePath.isEmpty() ? QString()
                     : QFileInfo(m_filePath).absolutePath();

    m_tracks.clear();
    const QJsonArray tracksArr = obj["tracks"].toArray();
    for (const auto& tVal : tracksArr) {
        Track track;
        track.fromJson(tVal.toObject(), projDir, m_pluginManager);
        m_tracks.push_back(std::move(track));
    }

    m_buses.clear();
    const QJsonArray busesArr = obj["buses"].toArray();
    if (busesArr.isEmpty()) {
        m_buses.push_back(makeDefaultMasterBus());
    } else {
        for (const auto& bVal : busesArr)
            m_buses.push_back(AudioBus::fromJson(bVal.toObject(), m_pluginManager));
    }

    if (m_buses.empty() || m_buses[0].name != "Master") {
        m_buses.insert(m_buses.begin(), makeDefaultMasterBus());
    }

    bool hasMetronome = (static_cast<int>(m_buses.size()) > MetronomeBusIndex
                         && m_buses[MetronomeBusIndex].name == "Metronome");
    if (!hasMetronome) {
        m_buses.insert(m_buses.begin() + MetronomeBusIndex, makeDefaultMetronomeBus());

        for (auto& track : m_tracks) {
            int busIdx = track.outputBusIndex();
            if (busIdx >= MetronomeBusIndex)
                track.setOutputBusIndex(busIdx + 1);
        }
        for (int i = 0; i < static_cast<int>(m_buses.size()); ++i) {
            if (i == MetronomeBusIndex) continue;
            int parent = m_buses[i].outputBusIndex;
            if (parent >= MetronomeBusIndex)
                m_buses[i].outputBusIndex = parent + 1;
        }
    }

    m_buses[0].removable = false;
    m_buses[0].outputBusIndex = -1;
    if (static_cast<int>(m_buses.size()) > MetronomeBusIndex) {
        m_buses[MetronomeBusIndex].removable = false;
    }

    m_instruments.clear();
    const QJsonArray instrumentsArr = obj["instruments"].toArray();
    for (const auto& iVal : instrumentsArr)
        m_instruments.push_back(Instrument::fromJson(iVal.toObject(), m_pluginManager));
}

int64_t Project::snapSample(int64_t sample, int beatDivision) const {
    double unit = samplesPerBeat() / static_cast<double>(beatDivision);
    double beats = sample / unit;
    int64_t snapped = static_cast<int64_t>(std::round(beats));
    return static_cast<int64_t>(snapped * unit);
}
