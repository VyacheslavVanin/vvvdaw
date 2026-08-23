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

void AudioEngine::generateClickEnvelope() {
    m_clickEnvelopeSize = m_sampleRate * kClickLengthMs / 1000;
    m_clickEnvelope.resize(m_clickEnvelopeSize);
    for (int i = 0; i < m_clickEnvelopeSize; ++i) {
        double t = static_cast<double>(i) / m_sampleRate;
        double decay = std::exp(-t * kClickDecayRate);
        m_clickEnvelope[i] = static_cast<float>(std::sin(2.0 * M_PI * kClickFrequency * t) * decay);
    }
}


void AudioEngine::renderClickSample(float* outL, float* outR, int64_t samplePos,
                                   double samplesPerBeat, double samplesPerBar, float gain) {
    double beatInBar = std::fmod(static_cast<double>(samplePos), samplesPerBar) / samplesPerBeat;
    int beatNum = static_cast<int>(std::floor(beatInBar));
    double beatFrac = beatInBar - std::floor(beatInBar);

    bool isDownbeat = (beatNum == 0);

    if (beatFrac < 1.0 / samplesPerBeat && m_clickPlayhead < 0) {
        m_clickPlayhead = 0;
        m_clickIsDownbeat = isDownbeat;
    }

    if (m_clickPlayhead >= 0 && m_clickPlayhead < m_clickEnvelopeSize) {
        float clickSample = m_clickEnvelope[m_clickPlayhead];
        if (!m_clickIsDownbeat)
            clickSample *= kDownbeatClickGain;
        *outL += clickSample * gain;
        *outR += clickSample * gain;
        m_clickPlayhead++;
    } else {
        m_clickPlayhead = -1;
    }
}


void AudioEngine::generateClick(Project* proj, float* buffer, unsigned long frameCount,
                                 int64_t pos, int outCh) {
    double samplesPerBeat = proj->samplesPerBeat();
    double samplesPerBar = proj->samplesPerBar();
    if (samplesPerBeat <= 0) return;

    for (unsigned long f = 0; f < frameCount; ++f)
        renderClickSample(&buffer[f * 2], &buffer[f * 2 + 1], pos + f,
                          samplesPerBeat, samplesPerBar, 1.0f);
}


void AudioEngine::processPrecounting(Project* proj, float* output, unsigned long frameCount,
                                      int outCh) {
    if (m_precountTotalSamples <= 0) {
        m_transportState.store(TransportState::Stopped, std::memory_order_release);
        return;
    }

    double samplesPerBeat = proj->samplesPerBeat();
    double samplesPerBar = proj->samplesPerBar();
    if (samplesPerBeat <= 0) return;

    float metroVol = 1.0f;
    if (static_cast<int>(proj->buses().size()) > 1)
        metroVol = proj->buses()[1].volume();

    for (unsigned long f = 0; f < frameCount; ++f) {
        if (m_precountPosition >= m_precountTotalSamples) break;

        renderClickSample(&output[f * 2], &output[f * 2 + 1], m_precountPosition,
                          samplesPerBeat, samplesPerBar, metroVol);
        m_precountPosition++;
    }

    if (m_precountPosition >= m_precountTotalSamples) {
        m_playPosition.store(m_precountStartPlayhead, std::memory_order_release);
        startPlayback();
        startRecording();
        m_transportState.store(TransportState::Recording, std::memory_order_release);
    }
}


void AudioEngine::startPrecount() {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj) return;
    m_precountStartPlayhead = m_playPosition.load(std::memory_order_acquire);
    m_precountTotalSamples = static_cast<int64_t>(proj->samplesPerBar());
    m_precountPosition = 0;
    m_clickPlayhead = -1;
}
