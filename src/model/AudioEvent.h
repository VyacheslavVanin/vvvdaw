#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <QJsonObject>

class AudioClip;

class AudioEvent {
public:
    AudioEvent() = default;

    bool isValid() const;
    int64_t endSample() const;

    void addTake(std::shared_ptr<AudioClip> takeClip);
    void setActiveTake(int index);
    const std::shared_ptr<AudioClip>& activeClip() const;

    int64_t id() const { return m_id; }
    void setId(int64_t id) { m_id = id; }

    const std::shared_ptr<AudioClip>& clip() const { return m_clip; }
    void setClip(const std::shared_ptr<AudioClip>& clip) { m_clip = clip; }

    int64_t startSample() const { return m_startSample; }
    void setStartSample(int64_t s) { m_startSample = s; }

    int64_t offsetSample() const { return m_offsetSample; }
    void setOffsetSample(int64_t s) { m_offsetSample = s; }

    int64_t durationSample() const { return m_durationSample; }
    void setDurationSample(int64_t s) { m_durationSample = s; }

    int64_t sourceFrames() const { return m_sourceFrames; }
    void setSourceFrames(int64_t s) { m_sourceFrames = s; }

    // Fade-in / fade-out lengths in samples, applied as an equal-power gain
    // ramp at the head/tail of the event. Adjacent events with opposing fades
    // form a crossfade that hides the discontinuity at their shared boundary.
    int64_t fadeInSamples() const { return m_fadeInSamples; }
    void setFadeInSamples(int64_t s) { m_fadeInSamples = s; }
    int64_t fadeOutSamples() const { return m_fadeOutSamples; }
    void setFadeOutSamples(int64_t s) { m_fadeOutSamples = s; }

    const std::vector<std::shared_ptr<AudioClip>>& takes() const { return m_takes; }
    std::vector<std::shared_ptr<AudioClip>>& takes() { return m_takes; }

    int activeTakeIndex() const { return m_activeTakeIndex; }
    void setActiveTakeIndex(int idx) { m_activeTakeIndex = idx; }

    QJsonObject toJson(const QString& projectDir = {}) const;
    static AudioEvent fromJson(const QJsonObject& obj, const QString& projectDir = {});

private:
    int64_t m_id = 0;
    std::shared_ptr<AudioClip> m_clip;
    int64_t m_startSample = 0;
    int64_t m_offsetSample = 0;
    int64_t m_durationSample = 0;
    int64_t m_sourceFrames = 0;
    int64_t m_fadeInSamples = 0;
    int64_t m_fadeOutSamples = 0;

    std::vector<std::shared_ptr<AudioClip>> m_takes;
    int m_activeTakeIndex = -1;
};
