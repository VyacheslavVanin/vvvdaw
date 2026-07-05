#pragma once
#include <cstdint>
#include <memory>
#include <vector>

class AudioClip;

struct AudioEvent {
    int64_t id = 0;
    std::shared_ptr<AudioClip> clip;
    int64_t startSample = 0;
    int64_t offsetSample = 0;
    int64_t durationSample = 0;

    std::vector<std::shared_ptr<AudioClip>> takes;
    int activeTakeIndex = -1;  // -1 = use clip directly (no takes)

    bool isValid() const;
    int64_t endSample() const;

    void addTake(std::shared_ptr<AudioClip> takeClip);
    void setActiveTake(int index);
    const std::shared_ptr<AudioClip>& activeClip() const;
};
