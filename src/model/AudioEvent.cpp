#include "AudioEvent.h"
#include "AudioClip.h"
#include "JsonUtils.h"
#include "EventTakeUtils.h"
#include <QJsonArray>

bool AudioEvent::isValid() const {
    return activeClip() && activeClip()->isValid();
}

int64_t AudioEvent::endSample() const {
    return m_startSample + m_durationSample;
}

void AudioEvent::addTake(std::shared_ptr<AudioClip> takeClip) {
    eventAddTake(m_takes, m_clip, std::move(takeClip), m_activeTakeIndex);
}

void AudioEvent::setActiveTake(int index) {
    eventSetActiveTake(m_takes, m_clip, m_activeTakeIndex, index);
}

const std::shared_ptr<AudioClip>& AudioEvent::activeClip() const {
    return eventActiveClip(m_takes, m_clip, m_activeTakeIndex);
}

QJsonObject AudioEvent::toJson(const QString& projectDir) const {
    QJsonObject eObj;
    if (m_clip) {
        eObj["clipPath"] = relativeToProject(m_clip->filePath(), projectDir);
        eObj["clipSampleRate"] = m_clip->sampleRate();
    }
    eObj["startSample"] = static_cast<qint64>(m_startSample);
    eObj["offsetSample"] = static_cast<qint64>(m_offsetSample);
    eObj["durationSample"] = static_cast<qint64>(m_durationSample);
    eObj["sourceFrames"] = static_cast<qint64>(m_sourceFrames);
    eObj["fadeInSamples"] = static_cast<qint64>(m_fadeInSamples);
    eObj["fadeOutSamples"] = static_cast<qint64>(m_fadeOutSamples);

    if (!m_takes.empty()) {
        QJsonArray takesArr;
        for (const auto& take : m_takes)
            takesArr.append(relativeToProject(take->filePath(), projectDir));
        eObj["takes"] = takesArr;
        eObj["activeTakeIndex"] = m_activeTakeIndex;
    }
    return eObj;
}

AudioEvent AudioEvent::fromJson(const QJsonObject& eObj, const QString& projectDir) {
    AudioEvent event;
    QString clipPath = eObj["clipPath"].toString();
    if (!clipPath.isEmpty()) {
        auto clip = std::make_shared<AudioClip>(resolveProjectPath(clipPath, projectDir));
        if (clip->isValid())
            event.setClip(clip);
    }
    event.setStartSample(jsonInt64(eObj, "startSample"));
    event.setOffsetSample(jsonInt64(eObj, "offsetSample"));
    event.setDurationSample(jsonInt64(eObj, "durationSample"));
    event.setSourceFrames(eObj.contains("sourceFrames")
        ? jsonInt64(eObj, "sourceFrames")
        : event.durationSample());
    event.setFadeInSamples(jsonInt64(eObj, "fadeInSamples"));
    event.setFadeOutSamples(jsonInt64(eObj, "fadeOutSamples"));

    if (eObj.contains("takes")) {
        const QJsonArray takesArr = eObj["takes"].toArray();
        for (const auto& takeVal : takesArr) {
            QString takePath = takeVal.toString();
            if (!takePath.isEmpty()) {
                auto takeClip = std::make_shared<AudioClip>(resolveProjectPath(takePath, projectDir));
                if (takeClip->isValid())
                    event.takes().push_back(takeClip);
            }
        }
        event.setActiveTakeIndex(eObj["activeTakeIndex"].toInt(-1));
        eventApplyActiveTake(event.takes(), event.m_clip, event.m_activeTakeIndex);
    }
    return event;
}
