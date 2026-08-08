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

QTEST_MAIN(TestAudio)
#include "test_audio.moc"
