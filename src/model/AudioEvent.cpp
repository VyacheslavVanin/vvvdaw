#include "AudioEvent.h"
#include "AudioClip.h"

bool AudioEvent::isValid() const {
    return activeClip() && activeClip()->isValid();
}

int64_t AudioEvent::endSample() const {
    return startSample + durationSample;
}

void AudioEvent::addTake(std::shared_ptr<AudioClip> takeClip) {
    takes.push_back(takeClip);
    activeTakeIndex = static_cast<int>(takes.size()) - 1;
    clip = takeClip;
}

void AudioEvent::setActiveTake(int index) {
    if (index >= 0 && index < static_cast<int>(takes.size())) {
        activeTakeIndex = index;
        clip = takes[index];
    }
}

const std::shared_ptr<AudioClip>& AudioEvent::activeClip() const {
    if (activeTakeIndex >= 0 && activeTakeIndex < static_cast<int>(takes.size()))
        return takes[activeTakeIndex];
    return clip;
}
