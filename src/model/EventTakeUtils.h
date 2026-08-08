#pragma once
#include <cstdint>
#include <memory>
#include <vector>

// Shared take-management helpers for events that hold a primary clip plus
// a list of alternative takes (see AudioEvent / MidiEvent).
template <typename ClipT>
inline void eventAddTake(std::vector<std::shared_ptr<ClipT>>& takes,
                         std::shared_ptr<ClipT>& clip,
                         std::shared_ptr<ClipT> takeClip,
                         int& activeTakeIndex) {
    takes.push_back(std::move(takeClip));
    activeTakeIndex = static_cast<int>(takes.size()) - 1;
    clip = takes.back();
}

template <typename ClipT>
inline void eventSetActiveTake(std::vector<std::shared_ptr<ClipT>>& takes,
                               std::shared_ptr<ClipT>& clip,
                               int& activeTakeIndex, int index) {
    if (index >= 0 && index < static_cast<int>(takes.size())) {
        activeTakeIndex = index;
        clip = takes[index];
    }
}

template <typename ClipT>
inline const std::shared_ptr<ClipT>& eventActiveClip(
        const std::vector<std::shared_ptr<ClipT>>& takes,
        const std::shared_ptr<ClipT>& clip,
        int activeTakeIndex) {
    if (activeTakeIndex >= 0 && activeTakeIndex < static_cast<int>(takes.size()))
        return takes[activeTakeIndex];
    return clip;
}

// Activate the take at activeTakeIndex when it is valid; used when loading
// an event from JSON so the active take becomes the primary clip.
template <typename ClipT>
inline void eventApplyActiveTake(std::vector<std::shared_ptr<ClipT>>& takes,
                                 std::shared_ptr<ClipT>& clip,
                                 int& activeTakeIndex) {
    if (activeTakeIndex >= 0 && activeTakeIndex < static_cast<int>(takes.size()))
        clip = takes[activeTakeIndex];
}
