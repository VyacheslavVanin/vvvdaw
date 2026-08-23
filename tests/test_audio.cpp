#include <QTest>
#include <QTemporaryDir>
#include <vector>
#include <cmath>
#include <chrono>
#include <thread>
#include "audio/RingBuffer.h"
#include "audio/AudioUtils.h"
#include "audio/RecordingPeaks.h"
#include "audio/RecordingManager.h"
#include "model/AudioClip.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"

class TestAudio : public QObject {
    Q_OBJECT
private slots:
    void ringBufferWriteRead();
    void ringBufferWrapAround();
    void ringBufferOverflow();
    void ringBufferReset();
    void ringBufferUsedAndFree();
    void panGains();
    void panStereoIdentity();
    void addSourceToBusMono();
    void addSourceToBusStereo();
    void writeTrackToBusMono();
    void writeTrackToBusStereo();
    void routeMonoToBusCentered();
    void linearToDecibelsMapping();
    void decibelsToLinearMapping();
    void busBufferPeakMax();
    void computeBusSoloPassSetLeaf();
    void computeBusSoloPassSetMaster();
    void computeBusSoloPassSetChain();
    void computeBusSoloFeedSetReverbScenario();
    void computeBusSoloFeedSetChain();
    void computeBusSendTapsPrePostMute();
    void computeBusProcessOrderChain();
    void computeBusProcessOrderRerouteAfterBusAdd();
    void computeBusProcessOrderFanOut();
    void recordingPeaksWindowMax();
    void recordingPeaksChunkBoundaries();
    void recordingPeaksFirstChannelOnly();
    void recordingPeaksFlushPartial();
    void recordingPeaksSnapshotIsCopy();
    void recordingManagerCollectsLivePeaks();
};

void TestAudio::ringBufferWriteRead() {
    RingBuffer buf(16);
    QCOMPARE(buf.used(), size_t(0));

    float src[5] = {1, 2, 3, 4, 5};
    QCOMPARE(buf.write(src, 5), size_t(5));
    QCOMPARE(buf.used(), size_t(5));

    float dst[5] = {0};
    QCOMPARE(buf.read(dst, 5), size_t(5));
    QCOMPARE(dst[0], 1.0f);
    QCOMPARE(dst[4], 5.0f);
    QCOMPARE(buf.used(), size_t(0));
}

void TestAudio::ringBufferWrapAround() {
    RingBuffer buf(8);
    float src[6] = {1, 2, 3, 4, 5, 6};
    QCOMPARE(buf.write(src, 6), size_t(6));

    float dst[4] = {0};
    QCOMPARE(buf.read(dst, 4), size_t(4));

    // writePos == 6, readPos == 4; writing again wraps past the end
    float src2[4] = {7, 8, 9, 10};
    QCOMPARE(buf.write(src2, 4), size_t(4));
    QCOMPARE(buf.used(), size_t(6));

    float dst2[6] = {0};
    QCOMPARE(buf.read(dst2, 6), size_t(6));
    QCOMPARE(dst2[0], 5.0f);
    QCOMPARE(dst2[1], 6.0f);
    QCOMPARE(dst2[2], 7.0f);
    QCOMPARE(dst2[5], 10.0f);
}

void TestAudio::ringBufferOverflow() {
    RingBuffer buf(4); // capacity 4, so 3 usable slots
    float src[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    QCOMPARE(buf.write(src, 10), size_t(3));
    QCOMPARE(buf.used(), size_t(3));

    float dst[3] = {0};
    QCOMPARE(buf.read(dst, 3), size_t(3));
    QCOMPARE(dst[0], 1.0f);
    QCOMPARE(dst[2], 3.0f);
}

void TestAudio::ringBufferReset() {
    RingBuffer buf(8);
    float src[3] = {1, 2, 3};
    buf.write(src, 3);
    buf.reset();
    QCOMPARE(buf.used(), size_t(0));
    QCOMPARE(buf.used(), size_t(0));
}

void TestAudio::ringBufferUsedAndFree() {
    RingBuffer buf(8); // 7 usable slots
    QCOMPARE(buf.used(), size_t(0));
    QCOMPARE(buf.freeSpace(), size_t(7));

    float src[4] = {1, 2, 3, 4};
    QCOMPARE(buf.write(src, 4), size_t(4));
    QCOMPARE(buf.used(), size_t(4));
    QCOMPARE(buf.freeSpace(), size_t(3));

    float dst[2] = {0};
    QCOMPARE(buf.read(dst, 2), size_t(2));
    QCOMPARE(buf.used(), size_t(2));
    QCOMPARE(buf.freeSpace(), size_t(5));
}

void TestAudio::panGains() {
    auto [l, r] = ::panGains(0.0f);
    QCOMPARE(l, 1.0f);
    QCOMPARE(r, 1.0f);
    std::tie(l, r) = ::panGains(-1.0f);
    QCOMPARE(l, 1.0f);
    QCOMPARE(r, 0.0f);
    std::tie(l, r) = ::panGains(1.0f);
    QCOMPARE(l, 0.0f);
    QCOMPARE(r, 1.0f);
}

void TestAudio::panStereoIdentity() {
    float lo, ro;
    panStereo(0.5f, -0.25f, 0.0f, lo, ro);
    QCOMPARE(lo, 0.5f);
    QCOMPARE(ro, -0.25f);

    // Full-left pan: mono source folds to the left at -3dB per original channel
    panStereo(0.5f, 0.5f, -1.0f, lo, ro);
    QCOMPARE(lo, 0.5f * 2.0f * 0.7071067811865476f); // (l+r)*k
    QCOMPARE(ro, 0.0f);

    // Full-right pan
    panStereo(0.5f, 0.5f, 1.0f, lo, ro);
    QCOMPARE(ro, 0.5f * 2.0f * 0.7071067811865476f);
    QCOMPARE(lo, 0.0f);
}

void TestAudio::addSourceToBusMono() {
    float bus[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    float src[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    addSourceToBus(bus, src, 1, 4, 1.0f, 0.0f, true);
    for (int i = 0; i < 8; ++i)
        QCOMPARE(bus[i], 0.5f); // mono -> duplicated to both channels at unity
}

void TestAudio::addSourceToBusStereo() {
    float bus[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    float src[8] = {1, 0, 1, 0, 1, 0, 1, 0};
    addSourceToBus(bus, src, 2, 4, 1.0f, 0.0f, false);
    for (int i = 0; i < 8; i += 2) {
        QCOMPARE(bus[i], 1.0f);
        QCOMPARE(bus[i + 1], 0.0f);
    }
}

void TestAudio::writeTrackToBusMono() {
    float bus[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    float trackL[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    float trackR[4] = {0, 0, 0, 0};
    writeTrackToBus(bus, trackL, trackR, 4, 1.0f, 0.0f, true);
    for (int i = 0; i < 8; ++i)
        QCOMPARE(bus[i], 0.5f); // mono -> duplicated at unity
}

void TestAudio::writeTrackToBusStereo() {
    float bus[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    float trackL[4] = {1, 1, 1, 1};
    float trackR[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    writeTrackToBus(bus, trackL, trackR, 4, 1.0f, 0.0f, false);
    for (int i = 0; i < 8; i += 2) {
        QCOMPARE(bus[i], 1.0f);
        QCOMPARE(bus[i + 1], 0.5f);
    }
}

void TestAudio::routeMonoToBusCentered() {
    float bus[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    float src[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    routeMonoToBus(bus, src, 4, 0.5f);
    // Each channel lands centered (equal L/R) scaled by the volume.
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(bus[i * 2], src[i] * 0.5f);
        QCOMPARE(bus[i * 2 + 1], src[i] * 0.5f);
    }
}

void TestAudio::linearToDecibelsMapping() {
    QCOMPARE(linearToDecibels(0.0f), -60.0f);
    QCOMPARE(linearToDecibels(1.0f), 0.0f);
    QVERIFY(std::abs(linearToDecibels(0.1f) - (-20.0f)) < 1e-4f);
    QVERIFY(std::abs(linearToDecibels(0.5f) - (-6.0206f)) < 1e-3f);
    // Above 0 dB is clamped to 0, very quiet signals clamp to -60.
    QCOMPARE(linearToDecibels(2.0f), 0.0f);
    QCOMPARE(linearToDecibels(1e-6f), -60.0f);
}

void TestAudio::decibelsToLinearMapping() {
    // Endpoints of the clamped scale.
    QCOMPARE(decibelsToLinear(0.0f), 1.0f);
    QCOMPARE(decibelsToLinear(-60.0f), 0.0f);
    QCOMPARE(decibelsToLinear(-90.0f), 0.0f); // below range clamps to silence
    QCOMPARE(decibelsToLinear(12.0f), 1.0f); // above range clamps to unity

    // 20 dB / 10 ratio, exact powers of ten.
    QVERIFY(std::abs(decibelsToLinear(-20.0f) - 0.1f) < 1e-6f);
    QVERIFY(std::abs(decibelsToLinear(-40.0f) - 0.01f) < 1e-7f);
    QVERIFY(std::abs(decibelsToLinear(-6.0f) - 0.501187f) < 1e-4f);

    // Round trips: converting to dB and back reproduces the linear value.
    for (float db : { -50.0f, -40.0f, -20.0f, -10.0f, -3.0f })
        QVERIFY(std::abs(linearToDecibels(decibelsToLinear(db)) - db) < 1e-3f);
    QVERIFY(std::abs(decibelsToLinear(linearToDecibels(0.5f)) - 0.5f) < 1e-3f);
}

void TestAudio::busBufferPeakMax() {
    float buf[8] = { 0.1f, -0.2f, 0.5f, 0.3f, -0.9f, 0.4f, 0.0f, 0.2f };
    QCOMPARE(busBufferPeak(buf, 4), 0.9f);
    float silent[4] = { 0, 0, 0, 0 };
    QCOMPARE(busBufferPeak(silent, 2), 0.0f);
}

void TestAudio::computeBusSoloPassSetLeaf() {
    // master(0, -> device), metronome(1, -> master), fx(2, -> master).
    std::vector<int> outputTo = { -1, 0, 0 };
    std::vector<bool> solo = { false, false, true };
    auto pass = computeBusSoloPassSet(outputTo, solo, 3);
    QVERIFY(pass[2]); // soloed fx
    QVERIFY(pass[0]); // master carries fx to the output
    QVERIFY(!pass[1]); // metronome is silenced
}

void TestAudio::computeBusSoloPassSetMaster() {
    std::vector<int> outputTo = { -1, 0, 0 };
    std::vector<bool> solo = { true, false, false };
    auto pass = computeBusSoloPassSet(outputTo, solo, 3);
    // Everything feeds master through its main output, so soling master keeps
    // the whole mix audible.
    QVERIFY(pass[0]);
    QVERIFY(pass[1]);
    QVERIFY(pass[2]);
}

void TestAudio::computeBusSoloPassSetChain() {
    // A(0, -> device), B(1, -> A), C(2, -> B), D(3, -> device).
    std::vector<int> outputTo = { -1, 0, 1, -1 };
    std::vector<bool> solo = { false, false, true, false };
    auto pass = computeBusSoloPassSet(outputTo, solo, 4);
    QVERIFY(pass[2]); // soloed C
    QVERIFY(pass[1]); // B feeds C and carries its signal onward
    QVERIFY(pass[0]); // A is C's route to the output
    QVERIFY(!pass[3]); // unrelated bus D is silenced

    // Without any solo the set is empty (the engine only consults it under solo).
    std::vector<bool> none = { false, false, false, false };
    auto empty = computeBusSoloPassSet(outputTo, none, 4);
    QVERIFY(!empty[0] && !empty[1] && !empty[2] && !empty[3]);
}

// The reverb scenario: dry(2) sends into soloed reverb(3). The feed set keeps
// dry alive so its send keeps feeding the reverb, while reverb is NOT in dry's
// feed set (it does not feed dry), so soloing dry silences the reverb.
void TestAudio::computeBusSoloFeedSetReverbScenario() {
    // master(0, -> device), metronome(1, -> master), dry(2, -> master),
    // reverb(3, -> master) with dry's send into reverb.
    std::vector<int> outputTo = { -1, 0, 0, 0 };
    std::vector<std::vector<int>> sendTargets = { {}, {}, { 3 }, {} };

    // Solo reverb: dry stays in the feed set (feeds via send), metronome and
    // master do not.
    std::vector<bool> soloReverb = { false, false, false, true };
    auto feed = computeBusSoloFeedSet(outputTo, sendTargets, soloReverb, 4);
    QVERIFY(feed[3]); // soloed reverb
    QVERIFY(feed[2]); // dry feeds reverb via its send
    QVERIFY(!feed[1]); // metronome feeds nothing relevant
    QVERIFY(!feed[0]); // master is a destination, not a feeder

    // Solo dry: reverb is not in the feed set (it does not feed dry), so the
    // engine will not tap dry's send into it and reverb stays silent.
    std::vector<bool> soloDry = { false, false, true, false };
    auto feedDry = computeBusSoloFeedSet(outputTo, sendTargets, soloDry, 4);
    QVERIFY(feedDry[2]); // soloed dry
    QVERIFY(!feedDry[3]); // reverb does not feed dry
}

// A bus feeding the soloed bus through its main-output chain stays in the feed
// set, so a group bus keeps its members' input when soloed.
void TestAudio::computeBusSoloFeedSetChain() {
    // master(0, -> device), metronome(1, -> master), group(2, -> master),
    // child(3, -> group).
    std::vector<int> outputTo = { -1, 0, 0, 2 };
    std::vector<std::vector<int>> sendTargets = { {}, {}, {}, {} };
    std::vector<bool> solo = { false, false, true, false };
    auto feed = computeBusSoloFeedSet(outputTo, sendTargets, solo, 4);
    QVERIFY(feed[2]); // soloed group
    QVERIFY(feed[3]); // child feeds group via its main output
    QVERIFY(!feed[1]); // metronome feeds master, not the group
}

// Pre-fader sends ignore the source fader and mute; post-fader sends follow the
// fader (mute = fader 0 drops them) and are scaled by volume * level; muted
// targets are dropped.
void TestAudio::computeBusSendTapsPrePostMute() {
    std::vector<int> targets = { 5, 6, 7 };
    std::vector<float> levels = { 1.0f, 0.5f, 0.25f };
    std::vector<bool> pre = { true, false, true };
    std::vector<bool> targetsMuted = { false, false, true };

    // Unmuted source, volume 0.8.
    auto taps = computeBusSendTaps(targets, levels, pre, targetsMuted, 0.8f, false);
    QCOMPARE(taps.size(), size_t(2));
    QCOMPARE(taps[0].first, 5);
    QVERIFY(std::abs(taps[0].second - 1.0f) < 1e-5f); // pre: level only
    QCOMPARE(taps[1].first, 6);
    QVERIFY(std::abs(taps[1].second - 0.4f) < 1e-5f); // post: volume * level

    // Muted source: pre-fader send still flows, post-fader send is dropped.
    auto tapsMuted = computeBusSendTaps(targets, levels, pre, targetsMuted, 0.8f, true);
    QCOMPARE(tapsMuted.size(), size_t(1));
    QCOMPARE(tapsMuted[0].first, 5);
    QVERIFY(std::abs(tapsMuted[0].second - 1.0f) < 1e-5f);

    // All post-fader sends follow the fader.
    auto tapsUnity = computeBusSendTaps(targets, levels, pre, targetsMuted, 1.0f, false);
    QCOMPARE(tapsUnity.size(), size_t(2));
    QVERIFY(std::abs(tapsUnity[1].second - 0.5f) < 1e-5f);
}

// Every bus must be processed before every bus it feeds, so the source signal
// is already in the target's buffer when the target runs. Buses routed to the
// output device are roots and come first.
void TestAudio::computeBusProcessOrderChain() {
    // Master(0, -> device), Metronome(1, -> master), FX(2, -> master),
    // Sub(3, -> FX).
    std::vector<std::vector<int>> targets = { { -1 }, { 0 }, { 0 }, { 2 } };
    auto order = computeBusProcessOrder(targets, 4);

    // All four buses appear exactly once.
    QCOMPARE(order.size(), size_t(4));
    std::vector<bool> seen(4, false);
    for (int b : order)
        seen[b] = true;
    QVERIFY(seen[0] && seen[1] && seen[2] && seen[3]);

    // Each bus precedes each of its targets (a valid topological order).
    for (int i = 0; i < 4; ++i) {
        for (int t : targets[static_cast<size_t>(i)]) {
            if (t < 0 || t >= 4) continue;
            auto itI = std::find(order.begin(), order.end(), i);
            auto itT = std::find(order.begin(), order.end(), t);
            QVERIFY(itI != order.end() && itT != order.end());
            QVERIFY(itI < itT);
        }
    }
}

// The reported regression: a project whose buses all feed master, then a new
// bus is added (default route -> master) and an existing bus is re-routed into
// it. Even though the count does not change on the re-route, the process order
// must put the re-routed bus before the new bus so its signal reaches master.
void TestAudio::computeBusProcessOrderRerouteAfterBusAdd() {
    // Master(0, -> device), Metronome(1, -> master), FX(2, -> master),
    // New(3, -> master): state right after adding bus 3.
    std::vector<std::vector<int>> beforeReroute = { { -1 }, { 0 }, { 0 }, { 0 } };
    auto orderBefore = computeBusProcessOrder(beforeReroute, 4);
    QCOMPARE(orderBefore.size(), size_t(4));

    // Re-route FX(2) into the new bus(3): count unchanged.
    std::vector<std::vector<int>> afterReroute = { { -1 }, { 0 }, { 3 }, { 0 } };
    auto order = computeBusProcessOrder(afterReroute, 4);

    QCOMPARE(order.size(), size_t(4));
    auto itFX = std::find(order.begin(), order.end(), 2);
    auto itNew = std::find(order.begin(), order.end(), 3);
    QVERIFY(itFX != order.end() && itNew != order.end());
    QVERIFY(itFX < itNew); // FX must be processed before the new bus

    // And the whole order is a valid topological order.
    for (int i = 0; i < 4; ++i) {
        for (int t : afterReroute[static_cast<size_t>(i)]) {
            if (t < 0 || t >= 4) continue;
            auto itI = std::find(order.begin(), order.end(), i);
            auto itT = std::find(order.begin(), order.end(), t);
            QVERIFY(itI != order.end() && itT != order.end());
            QVERIFY(itI < itT);
        }
    }
}

// Splitting: one bus fans its signal out to two targets (main output + send).
// Both targets must be processed after the source.
void TestAudio::computeBusProcessOrderFanOut() {
    // master(0, -> device), metronome(1, -> master), fx(2, -> master),
    // drums(3, -> master), fx sends into drums(2) via a send (target 3).
    std::vector<std::vector<int>> targets = { { -1 }, { 0 }, { 0, 3 }, { 0 } };
    auto order = computeBusProcessOrder(targets, 4);

    QCOMPARE(order.size(), size_t(4));
    std::vector<bool> seen(4, false);
    for (int b : order)
        seen[b] = true;
    QVERIFY(seen[0] && seen[1] && seen[2] && seen[3]);

    // FX(2) is processed before both its main target (0) and its send target
    // (3); the whole order is a valid topological order.
    for (int i = 0; i < 4; ++i) {
        for (int t : targets[static_cast<size_t>(i)]) {
            if (t < 0 || t >= 4) continue;
            auto itI = std::find(order.begin(), order.end(), i);
            auto itT = std::find(order.begin(), order.end(), t);
            QVERIFY(itI != order.end() && itT != order.end());
            QVERIFY(itI < itT);
        }
    }
}

void TestAudio::recordingPeaksWindowMax() {
    RecordingPeaks peaks;
    const size_t step = AudioClip::FINE_PEAK_STEP_FRAMES;

    // One full window at a constant amplitude yields a single peak.
    std::vector<float> s(step * 2, 0.5f);
    peaks.addSamples(s.data(), step, 2);
    auto snap = peaks.snapshot();
    QCOMPARE(snap.size(), size_t(1));
    QVERIFY(std::abs(snap[0].min - 0.5f) < 1e-6f);
    QVERIFY(std::abs(snap[0].max - 0.5f) < 1e-6f);
    QCOMPARE(peaks.recordedFrames(), int64_t(step));

    // A second window with a lower peak is appended.
    std::vector<float> s2(step * 2, 0.25f);
    peaks.addSamples(s2.data(), step, 2);
    snap = peaks.snapshot();
    QCOMPARE(snap.size(), size_t(2));
    QVERIFY(std::abs(snap[1].min - 0.25f) < 1e-6f);
    QVERIFY(std::abs(snap[1].max - 0.25f) < 1e-6f);
    QCOMPARE(peaks.recordedFrames(), int64_t(step) * 2);
}

void TestAudio::recordingPeaksChunkBoundaries() {
    RecordingPeaks peaks;
    const size_t step = AudioClip::FINE_PEAK_STEP_FRAMES;

    // Two full windows: the first at 0.8, the second at 0.3.
    std::vector<float> data(step * 2 * 2, 0.0f);
    std::fill(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(step * 2), 0.8f);
    std::fill(data.begin() + static_cast<std::ptrdiff_t>(step * 2),
              data.begin() + static_cast<std::ptrdiff_t>(step * 4), 0.3f);

    // Feed one window split across two calls with awkward frame counts so a
    // peak window spans several addSamples() calls.
    const size_t f1 = step / 3;
    const size_t f2 = step - f1; // completes the first window
    peaks.addSamples(data.data(), f1, 2);
    peaks.addSamples(data.data() + f1 * 2, f2, 2);

    auto snap = peaks.snapshot();
    QCOMPARE(snap.size(), size_t(1));
    QVERIFY(std::abs(snap[0].min - 0.8f) < 1e-6f);
    QVERIFY(std::abs(snap[0].max - 0.8f) < 1e-6f);
    QCOMPARE(peaks.recordedFrames(), int64_t(step));
}

void TestAudio::recordingPeaksFirstChannelOnly() {
    RecordingPeaks peaks;
    const size_t step = AudioClip::FINE_PEAK_STEP_FRAMES;

    // First channel carries the signal, second channel is silent: the peak
    // must reflect only the first channel (matching AudioClip peak behavior).
    std::vector<float> s(step * 2, 0.0f);
    for (size_t f = 0; f < step; ++f)
        s[f * 2] = 0.6f;
    peaks.addSamples(s.data(), step, 2);

    auto snap = peaks.snapshot();
    QCOMPARE(snap.size(), size_t(1));
    QVERIFY(std::abs(snap[0].min - 0.6f) < 1e-6f);
    QVERIFY(std::abs(snap[0].max - 0.6f) < 1e-6f);
}

void TestAudio::recordingPeaksFlushPartial() {
    RecordingPeaks peaks;
    const size_t step = AudioClip::FINE_PEAK_STEP_FRAMES;

    // Half a window: nothing in the snapshot until flushed.
    std::vector<float> s(step, 0.7f);
    peaks.addSamples(s.data(), step / 2, 2);
    QCOMPARE(peaks.snapshot().size(), size_t(0));

    peaks.flush();
    auto snap = peaks.snapshot();
    QCOMPARE(snap.size(), size_t(1));
    QVERIFY(std::abs(snap[0].min - 0.7f) < 1e-6f);
    QVERIFY(std::abs(snap[0].max - 0.7f) < 1e-6f);
}

void TestAudio::recordingPeaksSnapshotIsCopy() {
    RecordingPeaks peaks;
    const size_t step = AudioClip::FINE_PEAK_STEP_FRAMES;
    std::vector<float> s(step * 2, 0.9f);
    peaks.addSamples(s.data(), step, 2);

    auto snap = peaks.snapshot();
    QCOMPARE(snap.size(), size_t(1));
    snap[0].max = 0.0f; // mutating the copy must not affect the accumulator
    auto snap2 = peaks.snapshot();
    QVERIFY(std::abs(snap2[0].max - 0.9f) < 1e-6f);
}

namespace {
// Stops the writer thread even if an assertion aborts the test early.
struct RecordingStopGuard {
    RecordingManager* rm;
    Project* project;
    bool stopped = false;
    ~RecordingStopGuard() {
        if (!stopped)
            rm->stop(project);
    }
};
} // namespace

void TestAudio::recordingManagerCollectsLivePeaks() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Project project;
    project.setFilePath(tmp.path() + "/test.vvvdaw"); // audio dir inside tmp
    project.addTrack("A1");
    project.tracks()[0].setRecordArmed(true);

    RecordingManager rm;
    constexpr unsigned long frames = 48000; // 1 s @ 48 kHz
    rm.setScratchSize(frames * 2);
    rm.start(&project, 48000, 0);
    RecordingStopGuard guard{ &rm, &project };

    // Mono input is duplicated to stereo internally: constant 0.5 amplitude.
    std::vector<float> input(frames, 0.5f);
    rm.processCapture(input.data(), frames, 1);
    rm.notifyWriter();

    // Wait (bounded) for the writer thread to drain the captured audio.
    std::vector<AudioClip::Peak> peaks;
    int64_t recordedFrames = 0;
    bool drained = false;
    for (int i = 0; i < 400 && !drained; ++i) {
        if (rm.copyRecordPeaks(0, peaks, recordedFrames)
            && recordedFrames >= static_cast<int64_t>(frames))
            drained = true;
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    QVERIFY(drained);

    // One peak per full FINE_PEAK_STEP window; a constant 0.5 mono signal
    // gives 0.5 peaks across the whole capture.
    const size_t expectedPeaks = frames / AudioClip::FINE_PEAK_STEP_FRAMES;
    QCOMPARE(peaks.size(), expectedPeaks);
    for (const auto& p : peaks) {
        QVERIFY(std::abs(p.min - 0.5f) < 1e-4f);
        QVERIFY(std::abs(p.max - 0.5f) < 1e-4f);
    }

    guard.stopped = true;
    rm.stop(&project);

    // The final recorded event exists on the armed track.
    QCOMPARE(project.tracks()[0].events().size(), size_t(1));
    AudioEvent& ev = project.tracks()[0].events()[0];
    QCOMPARE(ev.startSample(), int64_t(0));
    QVERIFY(ev.durationSample() > 0);
}

QTEST_MAIN(TestAudio)
#include "test_audio.moc"
