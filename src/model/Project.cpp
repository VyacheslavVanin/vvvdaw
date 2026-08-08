#include "Project.h"
#include "AudioClip.h"
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

Project::Project()
    : m_name("Untitled")
{
    AudioBus master;
    master.name = "Master";
    master.volume = 1.0f;
    master.pan = 0.0f;
    master.outputBusIndex = -1;
    master.removable = false;
    m_buses.push_back(std::move(master));

    AudioBus metro;
    metro.name = "Metronome";
    metro.volume = static_cast<float>(vvvdaw::DefaultVolume);
    metro.pan = 0.0f;
    metro.outputBusIndex = 0;
    metro.removable = false;
    m_buses.push_back(std::move(metro));
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

    for (auto& instrument : m_instruments) {
        if (instrument.outputBusIndex() == index)
            instrument.setOutputBusIndex(0);
        else if (instrument.outputBusIndex() > index)
            instrument.setOutputBusIndex(instrument.outputBusIndex() - 1);
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
    for (const auto& track : m_tracks) {
        QJsonObject tObj;
        tObj["name"] = track.name();
        tObj["type"] = track.type() == Track::Type::Midi ? "midi" : "audio";
        tObj["channels"] = track.channels();
        tObj["inputDeviceId"] = track.inputDeviceId();
        tObj["inputChannel"] = track.inputChannel();
        tObj["outputBusIndex"] = track.outputBusIndex();
        tObj["pan"] = track.pan();
        tObj["volume"] = track.volume();
        tObj["muted"] = track.isMuted();
        tObj["solo"] = track.isSolo();

        if (track.type() == Track::Type::Midi) {
            tObj["midiOutputDeviceId"] = track.midiOutputDeviceId();
            if (!track.midiOutputDeviceName().isEmpty())
                tObj["midiOutputDeviceName"] = track.midiOutputDeviceName();
            tObj["instrumentIndex"] = track.instrumentIndex();
        }

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
            eObj["sourceFrames"] = static_cast<qint64>(event.sourceFrames());

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

        if (track.type() == Track::Type::Midi) {
            QJsonArray midiEventsArr;
            for (const auto& event : track.midiEvents()) {
                QJsonObject eObj;
                eObj["startSample"] = static_cast<qint64>(event.startSample());
                eObj["offsetSample"] = static_cast<qint64>(event.offsetSample());
                eObj["durationSample"] = static_cast<qint64>(event.durationSample());
                if (event.clip())
                    eObj["clip"] = event.clip()->toJson();
                if (!event.takes().empty()) {
                    QJsonArray takesArr;
                    for (const auto& take : event.takes())
                        takesArr.append(take->toJson());
                    eObj["takes"] = takesArr;
                    eObj["activeTakeIndex"] = event.activeTakeIndex();
                }
                midiEventsArr.append(eObj);
            }
            tObj["midiEvents"] = midiEventsArr;
        }

        if (track.pluginChain().count() > 0)
            tObj["plugins"] = track.pluginChain().toJson();
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
        bObj["solo"] = bus.solo;
        bObj["muted"] = bus.muted;
        bObj["removable"] = bus.removable;
        if (bus.pluginChain.count() > 0)
            bObj["plugins"] = bus.pluginChain.toJson();
        busesArr.append(bObj);
    }
    obj["buses"] = busesArr;

    QJsonArray instrumentsArr;
    for (const auto& instrument : m_instruments) {
        QJsonObject iObj;
        iObj["name"] = instrument.name();
        iObj["pan"] = instrument.pan();
        iObj["volume"] = instrument.volume();
        iObj["outputBusIndex"] = instrument.outputBusIndex();
        iObj["solo"] = instrument.isSolo();
        iObj["muted"] = instrument.isMuted();
        if (instrument.synth())
            iObj["synth"] = instrument.synth()->stateToJson();
        if (instrument.effects().count() > 0)
            iObj["effects"] = instrument.effects().toJson();
        instrumentsArr.append(iObj);
    }
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
        bool isMidi = tObj["type"].toString() == "midi";
        Track track(isMidi ? Track::Type::Midi : Track::Type::Audio,
                    tObj["name"].toString());
        track.setChannels(tObj["channels"].toInt(2));
        track.setInputDeviceId(tObj["inputDeviceId"].toInt(-1));
        track.setInputChannel(tObj["inputChannel"].toInt(0));
        track.setOutputBusIndex(tObj["outputBusIndex"].toInt(0));
        track.setPan(static_cast<float>(tObj["pan"].toDouble(0.0)));
        track.setVolume(static_cast<float>(tObj["volume"].toDouble(vvvdaw::DefaultVolume)));
        track.setMuted(tObj["muted"].toBool(false));
        track.setSolo(tObj["solo"].toBool(false));

        if (isMidi) {
            track.setMidiOutputDeviceId(tObj["midiOutputDeviceId"].toInt(-1));
            track.setMidiOutputDeviceName(tObj["midiOutputDeviceName"].toString());
            track.setInstrumentIndex(tObj["instrumentIndex"].toInt(-1));
        }

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
            event.setSourceFrames(eObj.contains("sourceFrames")
                ? static_cast<int64_t>(eObj["sourceFrames"].toVariant().toLongLong())
                : event.durationSample());

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

        if (isMidi) {
            const QJsonArray midiEventsArr = tObj["midiEvents"].toArray();
            for (const auto& eVal : midiEventsArr) {
                QJsonObject eObj = eVal.toObject();
                MidiEvent event;
                if (eObj.contains("clip")) {
                    auto clip = std::make_shared<MidiClip>();
                    clip->fromJson(eObj["clip"].toObject());
                    event.setClip(clip);
                }
                event.setStartSample(static_cast<int64_t>(eObj["startSample"].toVariant().toLongLong()));
                event.setOffsetSample(static_cast<int64_t>(eObj["offsetSample"].toVariant().toLongLong()));
                event.setDurationSample(static_cast<int64_t>(eObj["durationSample"].toVariant().toLongLong()));

                if (eObj.contains("takes")) {
                    const QJsonArray takesArr = eObj["takes"].toArray();
                    for (const auto& takeVal : takesArr) {
                        auto takeClip = std::make_shared<MidiClip>();
                        takeClip->fromJson(takeVal.toObject());
                        event.takes().push_back(takeClip);
                    }
                    event.setActiveTakeIndex(eObj["activeTakeIndex"].toInt(-1));
                    if (event.activeTakeIndex() >= 0 && event.activeTakeIndex() < static_cast<int>(event.takes().size()))
                        event.setClip(event.takes()[event.activeTakeIndex()]);
                }

                track.addMidiEvent(event);
            }
        }

        if (tObj.contains("plugins"))
            track.pluginChain().fromJson(tObj["plugins"].toObject(), m_pluginManager);
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
        m_buses.push_back(std::move(master));
    } else {
        for (const auto& bVal : busesArr) {
            QJsonObject bObj = bVal.toObject();
            AudioBus bus;
            bus.name = bObj["name"].toString("Bus");
            bus.pan = static_cast<float>(bObj["pan"].toDouble(0.0));
            bus.volume = static_cast<float>(bObj["volume"].toDouble(1.0));
            bus.outputBusIndex = bObj["outputBusIndex"].toInt(0);
            bus.solo = bObj["solo"].toBool(false);
            bus.muted = bObj["muted"].toBool(false);
            bus.removable = bObj["removable"].toBool(true);
            if (bObj.contains("plugins"))
                bus.pluginChain.fromJson(bObj["plugins"].toObject(), m_pluginManager);
            m_buses.push_back(std::move(bus));
        }
    }

    if (m_buses.empty() || m_buses[0].name != "Master") {
        AudioBus master;
        master.name = "Master";
        master.volume = 1.0f;
        master.pan = 0.0f;
        master.outputBusIndex = -1;
        master.removable = false;
        m_buses.insert(m_buses.begin(), std::move(master));
    }

    bool hasMetronome = (static_cast<int>(m_buses.size()) > MetronomeBusIndex
                         && m_buses[MetronomeBusIndex].name == "Metronome");
    if (!hasMetronome) {
        AudioBus metro;
        metro.name = "Metronome";
        metro.volume = static_cast<float>(vvvdaw::DefaultVolume);
        metro.pan = 0.0f;
        metro.outputBusIndex = 0;
        metro.removable = false;
        m_buses.insert(m_buses.begin() + MetronomeBusIndex, std::move(metro));

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
    for (const auto& iVal : instrumentsArr) {
        QJsonObject iObj = iVal.toObject();
        Instrument instrument;
        instrument.setName(iObj["name"].toString("Instrument"));
        instrument.setPan(static_cast<float>(iObj["pan"].toDouble(0.0)));
        instrument.setVolume(static_cast<float>(iObj["volume"].toDouble(1.0)));
        instrument.setOutputBusIndex(iObj["outputBusIndex"].toInt(0));
        instrument.setSolo(iObj["solo"].toBool(false));
        instrument.setMuted(iObj["muted"].toBool(false));
        if (iObj.contains("synth")) {
            auto synth = PluginChain::createInstance(iObj["synth"].toObject(), m_pluginManager);
            if (synth)
                instrument.setSynth(std::move(synth));
        }
        if (iObj.contains("effects"))
            instrument.effects().fromJson(iObj["effects"].toObject(), m_pluginManager);
        m_instruments.push_back(std::move(instrument));
    }
}

int64_t Project::snapSample(int64_t sample, int beatDivision) const {
    double unit = samplesPerBeat() / static_cast<double>(beatDivision);
    double beats = sample / unit;
    int64_t snapped = static_cast<int64_t>(std::round(beats));
    return static_cast<int64_t>(snapped * unit);
}
