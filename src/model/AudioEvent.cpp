#include "AudioEvent.h"
#include "AudioClip.h"

bool AudioEvent::isValid() const {
    return clip && clip->isValid();
}

int64_t AudioEvent::endSample() const {
    return startSample + durationSample;
}
