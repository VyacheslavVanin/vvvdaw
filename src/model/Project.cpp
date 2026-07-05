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

Project::Project()
    : m_name("Untitled")
{
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

    // Collect unique clips and copy external files to audio dir
    QSet<const AudioClip*> processedClips;
    auto saveClip = [&](std::shared_ptr<AudioClip>& clip) {
        if (!clip || processedClips.contains(clip.get()))
            return;
        processedClips.insert(clip.get());

        QString srcPath = clip->filePath();
        QString targetPath;
        if (srcPath.isEmpty()) {
            // Generated clip with no backing file — write it out
            QString name = QString("clip_%1.wav").arg(
                QString::number(reinterpret_cast<quintptr>(clip.get()), 16));
            targetPath = audioDir + "/" + name;
            clip->saveToFile(targetPath);
        } else {
            QFileInfo srcInfo(srcPath);
            QString srcAbs = srcInfo.absoluteFilePath();
            QString audioAbs = QDir(audioDir).absolutePath();

            if (srcInfo.absolutePath() == audioAbs) {
                // Already in audio dir
                targetPath = srcAbs;
            } else {
                // Copy to audio dir
                QString baseName = srcInfo.completeBaseName();
                QString ext = srcInfo.suffix();
                if (ext.isEmpty()) ext = "wav";
                targetPath = audioDir + "/" + baseName + "." + ext;

                // Handle name collisions
                int counter = 1;
                while (QFile::exists(targetPath)
                       && QFileInfo(targetPath).absoluteFilePath() != srcAbs) {
                    targetPath = audioDir + "/" + baseName + "_" + QString::number(counter++) + "." + ext;
                }

                // Copy if not already the same file
                if (QFileInfo(targetPath).absoluteFilePath() != srcAbs) {
                    if (!QFile::copy(srcPath, targetPath)) {
                        qWarning() << "Failed to copy audio file:" << srcPath << "→" << targetPath;
                        return;
                    }
                }
            }
        }

        clip->setFilePath(targetPath);
    };

    for (auto& track : m_tracks) {
        for (auto& event : track.events()) {
            saveClip(event.clip);
            for (auto& take : event.takes)
                saveClip(take);
        }
    }

    // Write project.json
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
    if (index < 0 || index >= static_cast<int>(m_buses.size()))
        return false;
    m_buses.erase(m_buses.begin() + index);
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
    obj["formatVersion"] = 1;
    obj["name"] = m_name;
    obj["sampleRate"] = m_sampleRate;
    obj["snapToGrid"] = m_snapToGrid;
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
            if (event.clip) {
                QString clipPath = event.clip->filePath();
                if (!projDir.isEmpty())
                    clipPath = relativePath(clipPath, projDir);
                eObj["clipPath"] = clipPath;
                eObj["clipSampleRate"] = event.clip->sampleRate();
            }
            eObj["startSample"] = static_cast<qint64>(event.startSample);
            eObj["offsetSample"] = static_cast<qint64>(event.offsetSample);
            eObj["durationSample"] = static_cast<qint64>(event.durationSample);

            // Takes
            if (!event.takes.empty()) {
                QJsonArray takesArr;
                for (const auto& take : event.takes) {
                    QString takePath = take->filePath();
                    if (!projDir.isEmpty())
                        takePath = relativePath(takePath, projDir);
                    takesArr.append(takePath);
                }
                eObj["takes"] = takesArr;
                eObj["activeTakeIndex"] = event.activeTakeIndex;
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
        bObj["deviceId"] = bus.deviceId;
        bObj["channel"] = bus.channel;
        busesArr.append(bObj);
    }
    obj["buses"] = busesArr;

    return obj;
}

void Project::fromJson(const QJsonObject& obj) {
    m_name = obj["name"].toString("Untitled");
    m_sampleRate = obj["sampleRate"].toInt(48000);
    m_snapToGrid = obj["snapToGrid"].toBool(true);

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
                // Resolve relative path
                QString absPath = QDir::isAbsolutePath(clipPath)
                    ? clipPath
                    : QDir(projDir).absoluteFilePath(clipPath);
                auto clip = std::make_shared<AudioClip>(absPath);
                if (clip->isValid())
                    event.clip = clip;
            }
            event.startSample = static_cast<int64_t>(eObj["startSample"].toVariant().toLongLong());
            event.offsetSample = static_cast<int64_t>(eObj["offsetSample"].toVariant().toLongLong());
            event.durationSample = static_cast<int64_t>(eObj["durationSample"].toVariant().toLongLong());

            // Takes
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
                            event.takes.push_back(takeClip);
                    }
                }
                event.activeTakeIndex = eObj["activeTakeIndex"].toInt(-1);
                if (event.activeTakeIndex >= 0 && event.activeTakeIndex < static_cast<int>(event.takes.size()))
                    event.clip = event.takes[event.activeTakeIndex];
            }

            track.addEvent(event);
        }
        m_tracks.push_back(std::move(track));
    }

    m_buses.clear();
    const QJsonArray busesArr = obj["buses"].toArray();
    for (const auto& bVal : busesArr) {
        QJsonObject bObj = bVal.toObject();
        AudioBus bus;
        bus.name = bObj["name"].toString();
        bus.deviceId = bObj["deviceId"].toInt(-1);
        bus.channel = bObj["channel"].toInt(0);
        m_buses.push_back(bus);
    }
}
