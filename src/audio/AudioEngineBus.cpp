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

void AudioEngine::rebuildBusGraph(Project* proj) {
    int busCount = static_cast<int>(proj->buses().size());
    m_busCount = busCount;

    m_busBuffers.resize(busCount);
    for (int i = 0; i < busCount; ++i)
        m_busBuffers[i].resize(static_cast<size_t>(m_bufferSize) * 2, 0.0f);

    {
        std::lock_guard<std::mutex> lock(m_meterMutex);
        m_busMeters.clear();
        for (int i = 0; i < busCount; ++i)
            m_busMeters.emplace_back();
    }

    std::vector<std::vector<int>> targets(static_cast<size_t>(busCount));
    for (int i = 0; i < busCount; ++i) {
        const auto& bus = proj->buses()[static_cast<size_t>(i)];
        std::vector<int> dests;
        dests.push_back(bus.outputBusIndex());
        for (const auto& send : bus.sends())
            dests.push_back(send.busIndex);
        targets[static_cast<size_t>(i)] = std::move(dests);
    }
    m_busOutputs = targets;
    m_busProcessOrder = computeBusProcessOrder(targets, busCount);
}


bool AudioEngine::busRoutingChanged(const Project* proj) const {
    const auto& buses = proj->buses();
    if (m_busOutputs.size() != buses.size()) return true;
    for (size_t i = 0; i < m_busOutputs.size(); ++i) {
        const auto& snapshot = m_busOutputs[i];
        if (snapshot.size() != 1 + buses[i].sends().size())
            return true;
        if (snapshot[0] != buses[i].outputBusIndex())
            return true;
        for (size_t s = 0; s < buses[i].sends().size(); ++s)
            if (snapshot[1 + s] != buses[i].sends()[s].busIndex)
                return true;
    }
    return false;
}


void AudioEngine::processBusMixing(Project* proj, float* output, unsigned long frameCount,
                                    int64_t pos, int outCh, const float* input, int inCh,
                                    bool monitoringOnly) {
    int busCount = static_cast<int>(proj->buses().size());
    if (busCount == 0) return;

    if (busCount != m_busCount || busRoutingChanged(proj))
        rebuildBusGraph(proj);

    for (int i = 0; i < busCount; ++i)
        std::fill(m_busBuffers[i].begin(), m_busBuffers[i].end(), 0.0f);

    ensureInstrumentMidiBuffers(static_cast<int>(proj->instruments().size()));
    for (auto& b : m_instrumentMidi)
        b.clear();

    bool firstActiveBlock = !m_midiTransportActive && !monitoringOnly;
    bool midiJumped = m_midiTransportActive &&
        (pos < m_lastMidiPos || pos - m_lastMidiPos > static_cast<int64_t>(frameCount) * 2);
    if ((firstActiveBlock && !m_activeMidiNotes.empty()) || midiJumped)
        flushActiveMidiNotes();
    // On transport start / seek, re-apply the current CC / pitch-bend value so
    // the destination never keeps a stale value from an earlier position.
    if (firstActiveBlock || midiJumped)
        reapplyControlState(proj, pos);
    // Mark active transport once playback blocks start so the discontinuity
    // flush only runs on real jumps. The flag is reset to false on any
    // explicit Stop or Pause (see setTransportState), which makes the next
    // playback block flush notes still held by instruments.
    if (!monitoringOnly)
        m_midiTransportActive = true;
    m_lastMidiPos = pos;

    mixTracksToBuses(proj, frameCount, pos, input, inCh, monitoringOnly);

    if (!monitoringOnly) {
        scheduleMidiTracks(proj, frameCount, pos);
        injectPreviewMidi();
        processInstruments(proj, frameCount);
    } else if (tickPreviewRender()) {
        injectPreviewMidi();
        processInstruments(proj, frameCount);
    }

    if (m_metronomeEnabled && !monitoringOnly && busCount > 1)
        generateClick(proj, m_busBuffers[1].data(), frameCount, pos, outCh);

    processBusChainsAndRoute(proj, output, frameCount, outCh);
}


void AudioEngine::mixTracksToBuses(Project* proj, unsigned long frameCount, int64_t pos,
                                   const float* input, int inCh, bool monitoringOnly) {
    int busCount = static_cast<int>(proj->buses().size());
    bool hasTrackSolo = anyTrackSolo(proj);

    int trackIndex = 0;
    for (const auto& track : proj->tracks()) {
        if (track.isMuted()) { ++trackIndex; continue; }
        if (hasTrackSolo && !track.isSolo()) { ++trackIndex; continue; }

        int busIdx = track.outputBusIndex();
        if (busIdx < 0 || busIdx >= busCount) busIdx = 0;

        float trackVol = track.volume();
        float pan = track.pan();

        bool isMono = track.channels() < 2;
        bool hasPlugins = track.pluginChain().count() > 0;
        bool hasAnyEvent = false;

        float* trackL = m_trackScratch.data();
        float* trackR = m_trackScratch.data() + frameCount;
        if (hasPlugins)
            std::fill(m_trackScratch.begin(), m_trackScratch.begin() + frameCount * 2, 0.0f);

        float* busBuf = m_busBuffers[busIdx].data();

        bool hasMonitor = track.isMonitoring() && input && inCh > 0;
        if (hasMonitor) {
            if (hasPlugins) {
                addSourceToTrack(trackL, trackR, input, inCh, frameCount, isMono);
                hasAnyEvent = true;
            } else {
                addSourceToBus(busBuf, input, inCh, frameCount, trackVol,
                               pan, isMono);
            }
        }

        if (!monitoringOnly) {
            for (const auto& event : track.events()) {
                auto activeClip = event.activeClip();
                if (!activeClip || !activeClip->isValid()) continue;

                int64_t eventEnd = event.startSample() + event.durationSample();
                if (pos >= eventEnd || pos + frameCount <= event.startSample())
                    continue;

                hasAnyEvent = true;
                size_t framesAvail = readEventBlock(event, trackIndex, pos,
                                                    m_stereoScratch.data(),
                                                    frameCount);
                if (framesAvail > 0) {
                    int ch = activeClip->channels();
                    // Apply the event's fade-in/fade-out gain envelope in place
                    // (equal-power curves) so adjacent events with opposing
                    // fades crossfade smoothly at their shared boundary.
                    const int64_t duration = event.durationSample();
                    const int64_t fadeIn = event.fadeInSamples();
                    const int64_t fadeOut = event.fadeOutSamples();
                    if ((fadeIn > 0 && fadeIn < duration) ||
                        (fadeOut > 0 && fadeOut < duration)) {
                        for (size_t f = 0; f < framesAvail; ++f) {
                            const float g = eventFadeGain(
                                pos + static_cast<int64_t>(f) - event.startSample(),
                                duration, fadeIn, fadeOut);
                            for (int c = 0; c < ch; ++c)
                                m_stereoScratch[f * static_cast<size_t>(ch) + static_cast<size_t>(c)] *= g;
                        }
                    }
                    if (hasPlugins) {
                        addSourceToTrack(trackL, trackR, m_stereoScratch.data(), ch,
                                         framesAvail, isMono);
                    } else {
                        addSourceToBus(busBuf, m_stereoScratch.data(), ch,
                                       framesAvail, trackVol, pan, isMono);
                    }
                }
            }
        }

        if (hasPlugins && hasAnyEvent) {
            float* inBufs[2] = { trackL, trackR };
            float* outBufs[2] = { trackL, trackR };
            int pluginChannels = isMono ? 1 : 2;
            track.pluginChain().process(inBufs, outBufs, frameCount, pluginChannels);

            writeTrackToBus(busBuf, trackL, trackR, frameCount, trackVol,
                            pan, isMono);
        }
        ++trackIndex;
    }
}


void AudioEngine::ensureMultiScratch(int channels) {
    if (channels <= 0) return;
    if (static_cast<int>(m_multiScratch.size()) < channels) {
        m_multiScratch.resize(static_cast<size_t>(channels));
        m_multiInBufs.resize(static_cast<size_t>(channels));
        m_multiOutBufs.resize(static_cast<size_t>(channels));
    }
    for (int c = 0; c < channels; ++c) {
        if (m_multiScratch[c].size() < static_cast<size_t>(m_bufferSize))
            m_multiScratch[c].resize(static_cast<size_t>(m_bufferSize), 0.0f);
        m_multiInBufs[c] = m_multiScratch[c].data();
        m_multiOutBufs[c] = m_multiScratch[c].data();
    }
}


void AudioEngine::processBusChainsAndRoute(Project* proj, float* output,
                                           unsigned long frameCount, int outCh) {
    int busCount = static_cast<int>(proj->buses().size());
    bool hasBusSolo = anyBusSolo(proj);

    // Under solo only the soloed buses and their main-output route to the
    // output are audible (soloPass). Everything that feeds a soloed bus stays
    // "alive" (soloFeed) so the soloed bus keeps its input chain: such buses
    // keep their sends into the feed set and route their main output onward
    // only while the parent is also in the feed set. A bus that only sends into
    // a soloed bus therefore does not leak its own raw output, but its send
    // still feeds the soloed bus.
    std::vector<int> outputTo(static_cast<size_t>(busCount), -1);
    std::vector<std::vector<int>> sendTargets(static_cast<size_t>(busCount));
    std::vector<bool> soloFlags(static_cast<size_t>(busCount), false);
    for (int i = 0; i < busCount; ++i) {
        const auto& b = proj->buses()[static_cast<size_t>(i)];
        outputTo[static_cast<size_t>(i)] = b.outputBusIndex();
        for (const auto& send : b.sends())
            sendTargets[static_cast<size_t>(i)].push_back(send.busIndex);
        soloFlags[static_cast<size_t>(i)] = b.isSolo();
    }
    std::vector<bool> soloPass;
    std::vector<bool> soloFeed;
    if (hasBusSolo) {
        soloPass = computeBusSoloPassSet(outputTo, soloFlags, busCount);
        soloFeed = computeBusSoloFeedSet(outputTo, sendTargets, soloFlags, busCount);
    }

    for (int idx : m_busProcessOrder) {
        const auto& bus = proj->buses()[static_cast<size_t>(idx)];

        if (bus.pluginChain().count() > 0) {
            float* buf = m_busBuffers[static_cast<size_t>(idx)].data();
            for (unsigned long f = 0; f < frameCount; ++f) {
                m_busDeinterleaveL[f] = buf[f * 2];
                m_busDeinterleaveR[f] = buf[f * 2 + 1];
            }
            float* inBufs[2] = { m_busDeinterleaveL.data(), m_busDeinterleaveR.data() };
            float* outBufs[2] = { m_busDeinterleaveL.data(), m_busDeinterleaveR.data() };
            bus.pluginChain().process(inBufs, outBufs, frameCount, 2);
            for (unsigned long f = 0; f < frameCount; ++f) {
                buf[f * 2]     = m_busDeinterleaveL[f];
                buf[f * 2 + 1] = m_busDeinterleaveR[f];
            }
        }

        bool busMuted = bus.isMuted();
        float bVol = bus.volume();
        float* buf = m_busBuffers[static_cast<size_t>(idx)].data();

        // Sends: pre-fader sends tap before the bus's fader and ignore mute;
        // post-fader sends follow the fader (a muted bus drops them). Under solo
        // a send only feeds targets that stay in the feed set, so a soloed send
        // destination keeps receiving while the sender's own output path is cut.
        if (!bus.sends().empty()) {
            std::vector<int> sTargets;
            std::vector<float> sLevels;
            std::vector<bool> sPre;
            std::vector<bool> sTargetMuted;
            sTargets.reserve(bus.sends().size());
            sLevels.reserve(bus.sends().size());
            sPre.reserve(bus.sends().size());
            sTargetMuted.reserve(bus.sends().size());
            for (const auto& send : bus.sends()) {
                sTargets.push_back(send.busIndex);
                sLevels.push_back(send.level);
                sPre.push_back(send.preFader);
                sTargetMuted.push_back(send.busIndex >= 0 && send.busIndex < busCount
                                           && proj->buses()[static_cast<size_t>(send.busIndex)].isMuted());
            }
            for (const auto& tap : computeBusSendTaps(sTargets, sLevels, sPre,
                                                      sTargetMuted, bVol, busMuted)) {
                int tIdx = tap.first;
                if (tIdx < 0 || tIdx >= busCount) continue;
                if (hasBusSolo && !soloFeed[static_cast<size_t>(tIdx)]) continue;
                float* dstBuf = m_busBuffers[static_cast<size_t>(tIdx)].data();
                for (unsigned long f = 0; f < frameCount; ++f) {
                    dstBuf[f * 2]     += buf[f * 2]     * tap.second;
                    dstBuf[f * 2 + 1] += buf[f * 2 + 1] * tap.second;
                }
            }
        }

        // Main output route: audible for soloed buses and their route to the
        // output; kept as a feed only while both this bus and its parent are in
        // the solo feed set (so the soloed bus's input chain stays intact but no
        // raw signal reaches the output from a non-soloed path).
        bool busPasses = !busMuted && (!hasBusSolo || soloPass[static_cast<size_t>(idx)]);
        bool busInFeed = !hasBusSolo || soloFeed[static_cast<size_t>(idx)];
        int parentIdx = bus.outputBusIndex();
        bool routeToOutput = (parentIdx < 0 || parentIdx >= busCount);

        bool routeActive = false;
        if (routeToOutput) {
            routeActive = busPasses;
        } else {
            bool parentInFeed = !hasBusSolo || soloFeed[static_cast<size_t>(parentIdx)];
            routeActive = busPasses || (!busMuted && busInFeed && parentInFeed);
        }

        float peak = routeActive ? busBufferPeak(buf, frameCount) * bVol : 0.0f;
        setBusMeter(idx, peak, peak >= 1.0f);
        if (!routeActive) continue;

        if (routeToOutput) {
            if (outCh >= 2) {
                for (unsigned long f = 0; f < frameCount; ++f) {
                    float lo, ro;
                    panStereo(buf[f * 2], buf[f * 2 + 1], bus.pan(), lo, ro);
                    output[f * 2]     += lo * bVol;
                    output[f * 2 + 1] += ro * bVol;
                }
            } else {
                for (unsigned long f = 0; f < frameCount; ++f) {
                    output[f] += (buf[f * 2] + buf[f * 2 + 1]) * kMonoDownmix * bVol;
                }
            }
        } else {
            float* dstBuf = m_busBuffers[static_cast<size_t>(parentIdx)].data();
            for (unsigned long f = 0; f < frameCount; ++f) {
                float lo, ro;
                panStereo(buf[f * 2], buf[f * 2 + 1], bus.pan(), lo, ro);
                dstBuf[f * 2]     += lo * bVol;
                dstBuf[f * 2 + 1] += ro * bVol;
            }
        }
    }
}


void AudioEngine::setBusMeter(int busIndex, float peak, bool clipped) {
    if (busIndex < 0 || busIndex >= static_cast<int>(m_busMeters.size()))
        return;
    m_busMeters[busIndex].peak.store(peak, std::memory_order_relaxed);
    if (clipped)
        m_busMeters[busIndex].clipped.store(true, std::memory_order_relaxed);
}


float AudioEngine::busMeterPeak(int busIndex) const {
    std::lock_guard<std::mutex> lock(m_meterMutex);
    if (busIndex < 0 || busIndex >= static_cast<int>(m_busMeters.size()))
        return 0.0f;
    return m_busMeters[busIndex].peak.load(std::memory_order_relaxed);
}


bool AudioEngine::busMeterClipping(int busIndex) const {
    std::lock_guard<std::mutex> lock(m_meterMutex);
    if (busIndex < 0 || busIndex >= static_cast<int>(m_busMeters.size()))
        return false;
    return m_busMeters[busIndex].clipped.load(std::memory_order_relaxed);
}


void AudioEngine::clearBusMeterClip(int busIndex) {
    std::lock_guard<std::mutex> lock(m_meterMutex);
    if (busIndex < 0 || busIndex >= static_cast<int>(m_busMeters.size()))
        return;
    m_busMeters[busIndex].clipped.store(false, std::memory_order_relaxed);
}
