#include <QTest>
#include <vector>
#include "audio/RingBuffer.h"
#include "audio/AudioUtils.h"

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
    void computeBusSoloPassSetSendFeed();
    void computeBusProcessOrderChain();
    void computeBusProcessOrderRerouteAfterBusAdd();
    void computeBusProcessOrderFanOut();
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
    std::vector<std::vector<int>> targets = { { -1 }, { 0 }, { 0 } };
    std::vector<bool> solo = { false, false, true };
    auto pass = computeBusSoloPassSet(targets, solo, 3);
    QVERIFY(pass[2]); // soloed fx
    QVERIFY(pass[0]); // master carries fx to the output
    QVERIFY(!pass[1]); // metronome is silenced
}

void TestAudio::computeBusSoloPassSetMaster() {
    std::vector<std::vector<int>> targets = { { -1 }, { 0 }, { 0 } };
    std::vector<bool> solo = { true, false, false };
    auto pass = computeBusSoloPassSet(targets, solo, 3);
    // Everything feeds master, so soling master keeps the whole mix audible.
    QVERIFY(pass[0]);
    QVERIFY(pass[1]);
    QVERIFY(pass[2]);
}

void TestAudio::computeBusSoloPassSetChain() {
    // A(0, -> device), B(1, -> A), C(2, -> B), D(3, -> device).
    std::vector<std::vector<int>> targets = { { -1 }, { 0 }, { 1 }, { -1 } };
    std::vector<bool> solo = { false, false, true, false };
    auto pass = computeBusSoloPassSet(targets, solo, 4);
    QVERIFY(pass[2]); // soloed C
    QVERIFY(pass[1]); // B feeds C and carries its signal onward
    QVERIFY(pass[0]); // A is C's route to the output
    QVERIFY(!pass[3]); // unrelated bus D is silenced

    // Without any solo the set is empty (the engine only consults it under solo).
    std::vector<bool> none = { false, false, false, false };
    auto empty = computeBusSoloPassSet(targets, none, 4);
    QVERIFY(!empty[0] && !empty[1] && !empty[2] && !empty[3]);
}

// A bus that feeds a soloed bus through a send edge stays audible, so the
// soloed bus receives its contribution.
void TestAudio::computeBusSoloPassSetSendFeed() {
    // master(0, -> device), metronome(1, -> master), fx(2, -> master),
    // drums(3, -> master) with a send into soloed fx(2).
    std::vector<std::vector<int>> targets = { { -1 }, { 0 }, { 0 }, { 0, 2 } };
    std::vector<bool> solo = { false, false, true, false };
    auto pass = computeBusSoloPassSet(targets, solo, 4);
    QVERIFY(pass[2]); // soloed fx
    QVERIFY(pass[0]); // master carries fx to the output
    QVERIFY(pass[3]); // drums feeds fx via the send
    QVERIFY(!pass[1]); // metronome is silenced
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

QTEST_MAIN(TestAudio)
#include "test_audio.moc"
