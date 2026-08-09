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
    void busBufferPeakMax();
    void computeBusSoloPassSetLeaf();
    void computeBusSoloPassSetMaster();
    void computeBusSoloPassSetChain();
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
    // Everything feeds master, so soling master keeps the whole mix audible.
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

QTEST_MAIN(TestAudio)
#include "test_audio.moc"
