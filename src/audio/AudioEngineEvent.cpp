#include "AudioEngine.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioBus.h"
#include "model/AudioClip.h"
#include "model/AudioEvent.h"
#include "model/Instrument.h"
#include "plugin/PluginChain.h"
#include "plugin/PluginInstance.h"
#include "AudioUtils.h"
#include "AudioEngineInternals.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <QDebug>

using vvvdaw::TransportState;
using namespace vvvdaw::audioengine;

void AudioEngine::clearStretchSlots() {
    m_stretchSlots.clear();
}


size_t AudioEngine::readEventBlock(const AudioEvent& event, int trackIndex,
                                   int64_t pos, float* out, size_t outFrames) {
    auto clip = event.activeClip();
    if (!clip || !clip->isValid())
        return 0;

    const int64_t duration = event.durationSample();
    int64_t srcFrames = event.sourceFrames();
    if (srcFrames <= 0)
        srcFrames = duration;
    if (duration <= 0 || srcFrames <= 0)
        return 0;

    const int64_t offset = event.offsetSample();
    const int64_t clipFrames = static_cast<int64_t>(clip->frameCount());
    const int64_t availSource = std::max<int64_t>(
        0, std::min<int64_t>(srcFrames, clipFrames - offset));

    const int ch = clip->channels();
    if (ch <= 0)
        return 0;

    const double rate = srcFrames / static_cast<double>(duration);

    if (std::fabs(rate - 1.0) < 1e-3) {
        if (clip->isStreaming()) {
            size_t framesAvail = 0;
            if (!m_streamingManager.readEvent(clip.get(), event.startSample(), duration,
                                              out, outFrames, ch, framesAvail))
                return 0;
            return framesAvail;
        }
        int64_t localPos = pos - event.startSample() + offset;
        if (localPos < offset)
            localPos = offset;
        int64_t limit = offset + availSource;
        int64_t validFrames = std::min<int64_t>(clipFrames, limit) - localPos;
        if (validFrames <= 0)
            return 0;
        size_t toCopy = static_cast<size_t>(std::min<int64_t>(
            validFrames, static_cast<int64_t>(outFrames)));
        std::memcpy(out, clip->data() + localPos * ch,
                    toCopy * static_cast<size_t>(ch) * sizeof(float));
        return toCopy;
    }

    const int64_t slotKey = (static_cast<int64_t>(trackIndex) << 32) |
                            (event.id() & 0xFFFFFFFFLL);
    auto& slot = m_stretchSlots[slotKey];
    bool freshSlot = false;
    if (slot.clip != clip.get() || slot.offset != offset || slot.srcFrames != srcFrames ||
        slot.duration != duration) {
        slot.clip = clip.get();
        slot.offset = offset;
        slot.srcFrames = srcFrames;
        slot.duration = duration;
        slot.ts.setChannels(ch);
        slot.ts.setSampleRate(m_sampleRate);
        slot.ts.reset();
        slot.srcCursor = offset + static_cast<int64_t>(
            std::llround(static_cast<double>(pos - event.startSample()) * rate));
        if (slot.srcCursor < offset)
            slot.srcCursor = offset;
        freshSlot = true;
    }

    size_t needIn = static_cast<size_t>(
        std::ceil(static_cast<double>(outFrames) * rate));
    if (freshSlot)
        needIn += TimeStretch::kWindowSize;
    int64_t remaining = (offset + availSource) - slot.srcCursor;
    if (remaining <= 0)
        return 0;
    size_t pushFrames = static_cast<size_t>(
        std::min<int64_t>(static_cast<int64_t>(needIn), remaining));
    size_t scratchFrames = m_sourceScratch.size() / static_cast<size_t>(ch);
    if (pushFrames > scratchFrames)
        pushFrames = scratchFrames;

    size_t pushed = 0;
    if (clip->isStreaming()) {
        if (pushFrames > 0 &&
            m_streamingManager.readEvent(clip.get(), event.startSample(), duration,
                                         m_sourceScratch.data(), pushFrames, ch, pushed)) {
            // pushed filled
        }
    } else {
        if (pushFrames > 0) {
            const float* src = clip->data() + slot.srcCursor * ch;
            std::memcpy(m_sourceScratch.data(), src,
                        pushFrames * static_cast<size_t>(ch) * sizeof(float));
            pushed = pushFrames;
        }
    }

    if (pushed > 0)
        slot.ts.push(m_sourceScratch.data(), pushed);
    slot.srcCursor += static_cast<int64_t>(pushed);

    return slot.ts.pull(out, outFrames, rate);
}
