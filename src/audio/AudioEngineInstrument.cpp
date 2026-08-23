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

void AudioEngine::processInstruments(Project* proj, unsigned long frameCount) {
    int instCount = static_cast<int>(proj->instruments().size());
    if (instCount == 0) return;

    bool anySolo = anyInstrumentSolo(proj);

    int busCount = static_cast<int>(proj->buses().size());
    for (int i = 0; i < instCount; ++i) {
        auto& inst = proj->instruments()[i];
        if (inst.isMuted() || !inst.synth()) {
            // Deliver pending note-offs (queued when the instrument or its
            // feeding track got muted) so held notes are released instead of
            // resuming after unmute. Output is discarded.
            if (inst.synth() && !m_instrumentMidi[i].empty()) {
                std::fill(m_instrumentScratchL.begin(), m_instrumentScratchL.begin() + frameCount, 0.0f);
                std::fill(m_instrumentScratchR.begin(), m_instrumentScratchR.begin() + frameCount, 0.0f);
                float* inBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };
                float* outBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };
                inst.synth()->process(inBufs, outBufs, frameCount, 2, &m_instrumentMidi[i]);
            }
            continue;
        }
        if (anySolo && !inst.isSolo()) continue;

        int busIdx = inst.outputBusIndex();
        if (busIdx < 0 || busIdx >= busCount) busIdx = 0;

        // Multi-channel routing: run the synth (and effects) with one buffer
        // per output channel, then accumulate each mapped channel into its bus.
        if (inst.isMultiChannel()) {
            int outCh = inst.synth()->audioOutputChannels();
            if (outCh < 1) outCh = 1;
            ensureMultiScratch(outCh);
            for (int c = 0; c < outCh; ++c)
                std::fill(m_multiScratch[c].begin(), m_multiScratch[c].begin() + frameCount, 0.0f);

            inst.synth()->process(m_multiInBufs.data(), m_multiOutBufs.data(),
                                  frameCount, outCh, &m_instrumentMidi[i]);
            if (inst.effects().count() > 0)
                inst.effects().process(m_multiInBufs.data(), m_multiOutBufs.data(),
                                       frameCount, outCh);

            const auto& routes = inst.channelRoutes();
            int nRoutes = std::min<int>(outCh, static_cast<int>(routes.size()));
            for (int c = 0; c < nRoutes; ++c) {
                int bIdx = routes[c].busIndex;
                if (bIdx < 0 || bIdx >= busCount) bIdx = 0;
                routeMonoToBus(m_busBuffers[bIdx].data(), m_multiScratch[c].data(),
                               frameCount, inst.volume());
            }
            continue;
        }

        float* busBuf = m_busBuffers[busIdx].data();

        std::fill(m_instrumentScratchL.begin(), m_instrumentScratchL.begin() + frameCount, 0.0f);
        std::fill(m_instrumentScratchR.begin(), m_instrumentScratchR.begin() + frameCount, 0.0f);
        float* inBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };
        float* outBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };

        inst.synth()->process(inBufs, outBufs, frameCount, 2, &m_instrumentMidi[i]);
        if (inst.effects().count() > 0)
            inst.effects().process(inBufs, outBufs, frameCount, 2);

        for (unsigned long f = 0; f < frameCount; ++f) {
            float lo, ro;
            panStereo(m_instrumentScratchL[f], m_instrumentScratchR[f], inst.pan(), lo, ro);
            busBuf[f * 2]     += lo * inst.volume();
            busBuf[f * 2 + 1] += ro * inst.volume();
        }
    }
}


void AudioEngine::ensureInstrumentMidiBuffers(int instCount) {
    if (static_cast<int>(m_instrumentMidi.size()) != instCount) {
        m_instrumentMidi.resize(static_cast<size_t>(instCount));
        for (auto& b : m_instrumentMidi)
            b.reserve(kInstrumentMidiReserve);
        m_instrumentCount = instCount;
    }
}


void AudioEngine::releaseInstruments() {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj) return;
    if (m_activeMidiNotes.empty()) return;

    int instCount = static_cast<int>(proj->instruments().size());
    if (instCount == 0 || m_instrumentScratchL.empty()) {
        m_activeMidiNotes.clear();
        return;
    }

    std::unique_lock projectLock(proj->mutex());

    ensureInstrumentMidiBuffers(instCount);
    for (int i = 0; i < instCount; ++i)
        m_instrumentMidi[i].clear();

    // Build one note-off per active note targeting each instrument.
    for (const auto& an : m_activeMidiNotes)
        sendNoteOff(an.destIndex, true, an.channel, an.pitch);

    // Render the release tails to silence so nothing is left ringing when
    // playback later resumes. Only run for instruments that actually held notes.
    const int maxReleaseBlocks = std::max(1, m_sampleRate / std::max(1, m_bufferSize));
    float* inBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };
    float* outBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };
    MidiBuffer emptyBuf;

    for (int i = 0; i < instCount; ++i) {
        auto& inst = proj->instruments()[i];
        if (m_instrumentMidi[i].empty()) continue;
        if (!inst.synth() || !inst.synth()->isActive()) continue;

        const bool multi = inst.isMultiChannel();
        int outCh = multi ? inst.synth()->audioOutputChannels() : 2;
        if (outCh < 1) outCh = 1;
        if (multi)
            ensureMultiScratch(outCh);

        float** inB = multi ? m_multiInBufs.data() : inBufs;
        float** outB = multi ? m_multiOutBufs.data() : outBufs;

        for (int b = 0; b < maxReleaseBlocks; ++b) {
            if (multi) {
                for (int c = 0; c < outCh; ++c)
                    std::fill(m_multiScratch[c].begin(), m_multiScratch[c].end(), 0.0f);
            } else {
                std::fill(m_instrumentScratchL.begin(), m_instrumentScratchL.end(), 0.0f);
                std::fill(m_instrumentScratchR.begin(), m_instrumentScratchR.end(), 0.0f);
            }
            const MidiBuffer* buf = (b == 0) ? &m_instrumentMidi[i] : &emptyBuf;
            inst.synth()->process(inB, outB, m_bufferSize, outCh, buf);
            if (inst.effects().count() > 0)
                inst.effects().process(inB, outB, m_bufferSize, outCh);

            float peak = 0.0f;
            for (int c = 0; c < outCh; ++c) {
                const float* chan = multi ? m_multiScratch[c].data()
                                          : (c == 0 ? m_instrumentScratchL.data()
                                                    : m_instrumentScratchR.data());
                for (int s = 0; s < m_bufferSize; ++s)
                    peak = std::max(peak, std::abs(chan[s]));
            }
            if (peak < kSilenceThreshold)
                break;
        }
    }

    m_activeMidiNotes.clear();
}
