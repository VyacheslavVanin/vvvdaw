#pragma once
#include <cstdint>
#include <memory>

class AudioClip;

struct AudioEvent {
    int64_t id = 0;
    std::shared_ptr<AudioClip> clip;
    int64_t startSample = 0;
    int64_t offsetSample = 0;
    int64_t durationSample = 0;

    bool isValid() const;
    int64_t endSample() const;
};
