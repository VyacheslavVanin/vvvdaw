#include "Project.h"
#include "AudioClip.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <cmath>

Project::Project()
    : m_name("Untitled")
{
    AudioBus master;
    master.name = "Master";
    master.volume = 1.0f;
    master.pan = 0.0f;
    master.outputBusIndex = -1;
    master.removable = false;
    m_buses.push_back(master);

    AudioBus metro;
    metro.name = "Metronome";
    metro.volume = 0.8f;
    metro.pan = 0.0f;
    metro.outputBusIndex = 0;
    metro.removable = false;
    m_buses.push_back(metro);
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

Track* Project::addTrack(const QString& name) {
    Track track(name.isEmpty() ? QString("Track %1").arg(m_tracks.size() + 1) : name);
    m_tracks.push_back(std::move(track));
    return &m_tracks.back();
}

bool Project::removeTrack(int index) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size()))
        return false;
    m_tracks.erase(m_tracks.begin() + index);
    return true;
}

int Project::addBus(const AudioBus& bus) {
    m_buses.push_back(bus);
    return static_cast<int>(m_buses.size()) - 1;
}

bool Project::removeBus(int index) {
    if (index <= 0 || index >= static_cast<int>(m_buses.size()))
        return false;
    if (!m_buses[index].removable)
        return false;

    m_buses.erase(m_buses.begin() + index);

    for (auto& track : m_tracks) {
        if (track.outputBusIndex() == index)
            track.setOutputBusIndex(0);
        else if (track.outputBusIndex() > index)
            track.setOutputBusIndex(track.outputBusIndex() - 1);
    }

    for (auto& bus : m_buses) {
        if (bus.outputBusIndex == index)
            bus.outputBusIndex = 0;
        else if (bus.outputBusIndex > index)
            bus.outputBusIndex -= 1;
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

static QString relativePath(const QString& filePath, const QString& projectDir) {
    QFileInfo fi(filePath);
    QString absPath = fi.absoluteFilePath();
    QString absProj = QFileInfo(projectDir).absoluteFilePath();
    if (absPath.startsWith(absProj + "/"))
        return absPath.mid(absProj.length() + 1);
    return absPath;
}

QJsonObject Project::toJson() const {
    QJsonObject obj;
    obj["formatVersion"] = 2;
    obj["name"] = m_name;
    obj["sampleRate"] = m_sampleRate;
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
    for (const auto& track : m_tracks) {
        QJsonObject tObj;
        tObj["name"] = track.name();
        tObj["inputDeviceId"] = track.inputDeviceId();
        tObj["inputChannel"] = track.inputChannel();
        tObj["outputBusIndex"] = track.outputBusIndex();
        tObj["pan"] = track.pan();
        tObj["volume"] = track.volume();
        tObj["muted"] = track.isMuted();
        tObj["solo"] = track.isSolo();

        QJsonArray eventsArr;
        for (const auto& event : track.events()) {
            QJsonObject eObj;
            if (event.clip()) {
                QString clipPath = event.clip()->filePath();
                if (!projDir.isEmpty())
                    clipPath = relativePath(clipPath, projDir);
                eObj["clipPath"] = clipPath;
                eObj["clipSampleRate"] = event.clip()->sampleRate();
            }
            eObj["startSample"] = static_cast<qint64>(event.startSample());
            eObj["offsetSample"] = static_cast<qint64>(event.offsetSample());
            eObj["durationSample"] = static_cast<qint64>(event.durationSample());

            if (!event.takes().empty()) {
                QJsonArray takesArr;
                for (const auto& take : event.takes()) {
                    QString takePath = take->filePath();
                    if (!projDir.isEmpty())
                        takePath = relativePath(takePath, projDir);
                    takesArr.append(takePath);
                }
                eObj["takes"] = takesArr;
                eObj["activeTakeIndex"] = event.activeTakeIndex();
            }

            eventsArr.append(eObj);
        }
        tObj["events"] = eventsArr;
        tracksArr.append(tObj);
    }
    obj["tracks"] = tracksArr;

    QJsonArray busesArr;
    for (const auto& bus : m_buses) {
        QJsonObject bObj;
        bObj["name"] = bus.name;
        bObj["pan"] = bus.pan;
        bObj["volume"] = bus.volume;
        bObj["outputBusIndex"] = bus.outputBusIndex;
        bObj["removable"] = bus.removable;
        busesArr.append(bObj);
    }
    obj["buses"] = busesArr;

    return obj;
}

void Project::fromJson(const QJsonObject& obj) {
    m_name = obj["name"].toString("Untitled");
    m_sampleRate = obj["sampleRate"].toInt(48000);
    m_snapToGrid = obj["snapToGrid"].toBool(true);
    m_metronomeEnabled = obj["metronomeEnabled"].toBool(false);
    m_precountEnabled = obj["precountEnabled"].toBool(false);
    m_tempo = obj["tempo"].toDouble(120.0);
    m_timeSigNum = obj["timeSigNum"].toInt(4);
    m_timeSigDen = obj["timeSigDen"].toInt(4);

    if (obj.contains("loopStart") && obj.contains("loopEnd")) {
        m_loopStart = static_cast<int64_t>(obj["loopStart"].toVariant().toLongLong());
        m_loopEnd = static_cast<int64_t>(obj["loopEnd"].toVariant().toLongLong());
    }
    if (obj.contains("recordRegionStart") && obj.contains("recordRegionEnd")) {
        m_recordRegionStart = static_cast<int64_t>(obj["recordRegionStart"].toVariant().toLongLong());
        m_recordRegionEnd = static_cast<int64_t>(obj["recordRegionEnd"].toVariant().toLongLong());
    }

    QString projDir = m_filePath.isEmpty() ? QString()
                     : QFileInfo(m_filePath).absolutePath();

    m_tracks.clear();
    const QJsonArray tracksArr = obj["tracks"].toArray();
    for (const auto& tVal : tracksArr) {
        QJsonObject tObj = tVal.toObject();
        Track track(tObj["name"].toString());
        track.setInputDeviceId(tObj["inputDeviceId"].toInt(-1));
        track.setInputChannel(tObj["inputChannel"].toInt(0));
        track.setOutputBusIndex(tObj["outputBusIndex"].toInt(0));
        track.setPan(static_cast<float>(tObj["pan"].toDouble(0.0)));
        track.setVolume(static_cast<float>(tObj["volume"].toDouble(0.8)));
        track.setMuted(tObj["muted"].toBool(false));
        track.setSolo(tObj["solo"].toBool(false));

        const QJsonArray eventsArr = tObj["events"].toArray();
        for (const auto& eVal : eventsArr) {
            QJsonObject eObj = eVal.toObject();
            AudioEvent event;
            QString clipPath = eObj["clipPath"].toString();
            if (!clipPath.isEmpty()) {
                QString absPath = QDir::isAbsolutePath(clipPath)
                    ? clipPath
                    : QDir(projDir).absoluteFilePath(clipPath);
                auto clip = std::make_shared<AudioClip>(absPath);
                if (clip->isValid())
                    event.setClip(clip);
            }
            event.setStartSample(static_cast<int64_t>(eObj["startSample"].toVariant().toLongLong()));
            event.setOffsetSample(static_cast<int64_t>(eObj["offsetSample"].toVariant().toLongLong()));
            event.setDurationSample(static_cast<int64_t>(eObj["durationSample"].toVariant().toLongLong()));

            if (eObj.contains("takes")) {
                const QJsonArray takesArr = eObj["takes"].toArray();
                for (const auto& takeVal : takesArr) {
                    QString takePath = takeVal.toString();
                    if (!takePath.isEmpty()) {
                        QString absPath = QDir::isAbsolutePath(takePath)
                            ? takePath
                            : QDir(projDir).absoluteFilePath(takePath);
                        auto takeClip = std::make_shared<AudioClip>(absPath);
                        if (takeClip->isValid())
                            event.takes().push_back(takeClip);
                    }
                }
                event.setActiveTakeIndex(eObj["activeTakeIndex"].toInt(-1));
                if (event.activeTakeIndex() >= 0 && event.activeTakeIndex() < static_cast<int>(event.takes().size()))
                    event.setClip(event.takes()[event.activeTakeIndex()]);
            }

            track.addEvent(event);
        }
        m_tracks.push_back(std::move(track));
    }

    m_buses.clear();
    const QJsonArray busesArr = obj["buses"].toArray();
    if (busesArr.isEmpty()) {
        AudioBus master;
        master.name = "Master";
        master.volume = 1.0f;
        master.pan = 0.0f;
        master.outputBusIndex = -1;
        master.removable = false;
        m_buses.push_back(master);
    } else {
        for (const auto& bVal : busesArr) {
            QJsonObject bObj = bVal.toObject();
            AudioBus bus;
            bus.name = bObj["name"].toString("Bus");
            bus.pan = static_cast<float>(bObj["pan"].toDouble(0.0));
            bus.volume = static_cast<float>(bObj["volume"].toDouble(1.0));
            bus.outputBusIndex = bObj["outputBusIndex"].toInt(0);
            bus.removable = bObj["removable"].toBool(true);
            m_buses.push_back(bus);
        }
    }

    if (m_buses.empty() || m_buses[0].name != "Master") {
        AudioBus master;
        master.name = "Master";
        master.volume = 1.0f;
        master.pan = 0.0f;
        master.outputBusIndex = -1;
        master.removable = false;
        m_buses.insert(m_buses.begin(), master);
    }

    bool hasMetronome = (static_cast<int>(m_buses.size()) > MetronomeBusIndex
                         && m_buses[MetronomeBusIndex].name == "Metronome");
    if (!hasMetronome) {
        AudioBus metro;
        metro.name = "Metronome";
        metro.volume = 0.8f;
        metro.pan = 0.0f;
        metro.outputBusIndex = 0;
        metro.removable = false;
        m_buses.insert(m_buses.begin() + MetronomeBusIndex, metro);

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
}

int64_t Project::snapSample(int64_t sample, int beatDivision) const {
    double unit = samplesPerBeat() / static_cast<double>(beatDivision);
    double beats = sample / unit;
    int64_t snapped = static_cast<int64_t>(std::round(beats));
    return static_cast<int64_t>(snapped * unit);
}
