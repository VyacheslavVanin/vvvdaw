#include "MidiEvent.h"
#include "JsonUtils.h"
#include "EventTakeUtils.h"
#include <QJsonArray>

int64_t MidiEvent::endSample() const {
    return m_startSample + m_durationSample;
}

void MidiEvent::addTake(std::shared_ptr<MidiClip> takeClip) {
    eventAddTake(m_takes, m_clip, std::move(takeClip), m_activeTakeIndex);
}

void MidiEvent::setActiveTake(int index) {
    eventSetActiveTake(m_takes, m_clip, m_activeTakeIndex, index);
}

const std::shared_ptr<MidiClip>& MidiEvent::activeClip() const {
    return eventActiveClip(m_takes, m_clip, m_activeTakeIndex);
}

QJsonObject MidiEvent::toJson() const {
    QJsonObject eObj;
    eObj["startSample"] = static_cast<qint64>(m_startSample);
    eObj["offsetSample"] = static_cast<qint64>(m_offsetSample);
    eObj["durationSample"] = static_cast<qint64>(m_durationSample);
    if (m_clip)
        eObj["clip"] = m_clip->toJson();
    if (!m_takes.empty()) {
        QJsonArray takesArr;
        for (const auto& take : m_takes)
            takesArr.append(take->toJson());
        eObj["takes"] = takesArr;
        eObj["activeTakeIndex"] = m_activeTakeIndex;
    }
    return eObj;
}

MidiEvent MidiEvent::fromJson(const QJsonObject& eObj) {
    MidiEvent event;
    if (eObj.contains("clip")) {
        auto clip = std::make_shared<MidiClip>();
        clip->fromJson(eObj["clip"].toObject());
        event.setClip(clip);
    }
    event.setStartSample(jsonInt64(eObj, "startSample"));
    event.setOffsetSample(jsonInt64(eObj, "offsetSample"));
    event.setDurationSample(jsonInt64(eObj, "durationSample"));

    if (eObj.contains("takes")) {
        const QJsonArray takesArr = eObj["takes"].toArray();
        for (const auto& takeVal : takesArr) {
            auto takeClip = std::make_shared<MidiClip>();
            takeClip->fromJson(takeVal.toObject());
            event.takes().push_back(takeClip);
        }
        event.setActiveTakeIndex(eObj["activeTakeIndex"].toInt(-1));
        eventApplyActiveTake(event.takes(), event.m_clip, event.m_activeTakeIndex);
    }
    return event;
}
