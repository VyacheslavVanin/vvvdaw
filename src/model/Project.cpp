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
    for (auto& track : m_tracks) {
        for (auto& event : track.events()) {
            if (!event.clip || processedClips.contains(event.clip.get()))
                continue;
            processedClips.insert(event.clip.get());

            QString srcPath = event.clip->filePath();
            QString targetPath;
            if (srcPath.isEmpty()) {
                // Generated clip with no backing file — write it out
                QString name = QString("clip_%1.wav").arg(
                    QString::number(reinterpret_cast<quintptr>(event.clip.get()), 16));
                targetPath = audioDir + "/" + name;
                event.clip->saveToFile(targetPath);
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
                            continue;
                        }
                    }
                }
            }

            event.clip->setFilePath(targetPath);
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
