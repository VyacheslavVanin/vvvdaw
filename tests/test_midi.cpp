#include <QTest>
#include <vector>
#include "midi/MidiMessageRing.h"
#include "midi/MidiInputManager.h"
#include "audio/MidiRecorder.h"
#include "audio/AudioEngine.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/Instrument.h"
#include "model/MidiEvent.h"
#include "model/MidiClip.h"

class TestMidi : public QObject {
    Q_OBJECT
private slots:
    void ringPushPop();
    void ringWrapAndOverflow();
    void parseNoteOn();
    void parseNoteOffVariants();
    void parseCc();
    void parseRejectsMalformed();
    void recorderRecordsIntoArmedTrack();
    void recorderIgnoresUnarmedTrack();
    void recorderFinalizesHeldNotes();
    void recorderRecordsIntoHintEvent();
    void openFirstDeviceWhenPresent();
    void cancelReleasesHeldPreviewNotes();
};

void TestMidi::ringPushPop() {
    MidiRingBuffer<MidiMessage> ring(4);
    QCOMPARE(ring.capacity(), size_t(4));
    QVERIFY(ring.empty());

    MidiMessage a;
    a.status = 0x90; a.data1 = 60; a.data2 = 100;
    MidiMessage b;
    b.status = 0x80; b.data1 = 60; b.data2 = 0;

    QCOMPARE(ring.push(a), size_t(1));
    QCOMPARE(ring.push(b), size_t(1));
    QCOMPARE(ring.used(), size_t(2));

    MidiMessage out;
    QVERIFY(ring.pop(out));
    QCOMPARE(int(out.status), 0x90);
    QCOMPARE(int(out.data1), 60);
    QCOMPARE(int(out.data2), 100);
    QCOMPARE(ring.used(), size_t(1));
    QVERIFY(ring.pop(out));
    QCOMPARE(int(out.status), 0x80);
    QVERIFY(ring.empty());
    QVERIFY(!ring.pop(out));
}

void TestMidi::ringWrapAndOverflow() {
    MidiRingBuffer<MidiMessage> ring(4); // 3 usable slots
    MidiMessage m;
    m.status = 0x90;
    for (int i = 0; i < 3; ++i) {
        m.data1 = static_cast<uint8_t>(i);
        QCOMPARE(ring.push(m), size_t(1));
    }
    QCOMPARE(ring.push(m), size_t(0)); // full

    MidiMessage out;
    QVERIFY(ring.pop(out));
    QCOMPARE(int(out.data1), 0);

    m.data1 = 99;
    QCOMPARE(ring.push(m), size_t(1)); // wraps past the end

    QVERIFY(ring.pop(out));
    QCOMPARE(int(out.data1), 1);
    QVERIFY(ring.pop(out));
    QCOMPARE(int(out.data1), 2);
    QVERIFY(ring.pop(out));
    QCOMPARE(int(out.data1), 99);
    QVERIFY(ring.empty());
}

void TestMidi::parseNoteOn() {
    std::vector<uint8_t> msg = { 0x90, 60, 100 };
    MidiMessage m;
    QVERIFY(MidiInputManager::parseMessage(msg, m));
    QVERIFY(m.isNoteOn());
    QVERIFY(!m.isNoteOff());
    QCOMPARE(int(m.status), 0x90);
    QCOMPARE(int(m.data1), 60);
    QCOMPARE(int(m.data2), 100);
}

void TestMidi::parseNoteOffVariants() {
    MidiMessage m;

    std::vector<uint8_t> noteOff80 = { 0x80, 60, 0 };
    QVERIFY(MidiInputManager::parseMessage(noteOff80, m));
    QVERIFY(m.isNoteOff());
    QVERIFY(!m.isNoteOn());

    // Note On with velocity 0 is a note-off.
    std::vector<uint8_t> noteOnVel0 = { 0x90, 62, 0 };
    QVERIFY(MidiInputManager::parseMessage(noteOnVel0, m));
    QVERIFY(m.isNoteOff());
    QVERIFY(!m.isNoteOn());
}

void TestMidi::parseCc() {
    std::vector<uint8_t> msg = { 0xB0, 110, 127 };
    MidiMessage m;
    QVERIFY(MidiInputManager::parseMessage(msg, m));
    QCOMPARE(int(m.status & 0xF0), 0xB0);
    QCOMPARE(int(m.data1), 110);
    QCOMPARE(int(m.data2), 127);
}

void TestMidi::parseRejectsMalformed() {
    MidiMessage m;
    std::vector<uint8_t> noStatus = { 0x3C, 60 };
    QVERIFY(!MidiInputManager::parseMessage(noStatus, m));
    std::vector<uint8_t> empty;
    QVERIFY(!MidiInputManager::parseMessage(empty, m));
}

void TestMidi::recorderRecordsIntoArmedTrack() {
    Project project;
    Track* armed = project.addMidiTrack("Midi 1");
    armed->setRecordArmed(true);
    project.addMidiTrack("Midi 2");

    MidiRecorder rec;
    // Not recording: no-op.
    QVERIFY(!rec.pump(project, false, 0, 0));

    // Begin recording (first pump with recording=true).
    const int64_t recordStart = 0;
    QVERIFY(!rec.pump(project, true, 0, recordStart));

    rec.captureNote(100000, 60, 100, true);
    rec.captureNote(100250, 60, 100, false);
    rec.captureNote(200000, 64, 77, true);
    QVERIFY(rec.pump(project, true, 250000, recordStart));

    auto& armedEvents = project.tracks()[0].midiEvents();
    QCOMPARE(armedEvents.size(), size_t(1));
    MidiEvent* ev = &armedEvents[0];
    QCOMPARE(ev->startSample(), int64_t(recordStart));
    MidiClip* clip = ev->activeClip().get();
    QVERIFY(clip);
    QCOMPARE(clip->notes().size(), size_t(2));

    const MidiNote* n60 = nullptr;
    const MidiNote* n64 = nullptr;
    for (const auto& n : clip->notes()) {
        if (n.pitch == 60) n60 = &n;
        if (n.pitch == 64) n64 = &n;
    }
    QVERIFY(n60);
    QVERIFY(n64);
    QCOMPARE(n60->velocity, 100);
    QCOMPARE(n60->startTick, project.samplesToTicks(100000));
    QCOMPARE(n60->durationTicks, project.samplesToTicks(100250) - project.samplesToTicks(100000));
    QCOMPARE(n64->velocity, 77);

    // Unarmed track got no event.
    QCOMPARE(project.tracks()[1].midiEvents().size(), size_t(0));
}

void TestMidi::recorderIgnoresUnarmedTrack() {
    Project project;
    project.addMidiTrack("Midi 1"); // not armed

    MidiRecorder rec;
    rec.captureNote(1000, 60, 100, true);
    QVERIFY(!rec.pump(project, true, 1000, 0));
    QCOMPARE(project.tracks()[0].midiEvents().size(), size_t(0));
}

void TestMidi::recorderFinalizesHeldNotes() {
    Project project;
    Track* armed = project.addMidiTrack("Midi 1");
    armed->setRecordArmed(true);

    MidiRecorder rec;
    const int64_t recordStart = 0;
    rec.pump(project, true, 0, recordStart);
    rec.captureNote(200000, 64, 77, true);
    QVERIFY(rec.pump(project, true, 200000, recordStart));

    // Stop recording: held note is released with the current duration.
    QVERIFY(!rec.pump(project, false, 250000, recordStart));
    QVERIFY(!rec.isRecording());

    auto& events = project.tracks()[0].midiEvents();
    QCOMPARE(events.size(), size_t(1));
    MidiEvent* ev = &events[0];
    MidiClip* clip = ev->activeClip().get();
    QCOMPARE(clip->notes().size(), size_t(1));
    const MidiNote& n = clip->notes()[0];
    QCOMPARE(n.pitch, 64);
    QCOMPARE(n.startTick, project.samplesToTicks(200000));
    QCOMPARE(n.durationTicks, project.samplesToTicks(250000) - project.samplesToTicks(200000));

    // Created event is finalized: start at record start, duration covers notes.
    QCOMPARE(ev->startSample(), int64_t(recordStart));
    QVERIFY(ev->durationSample() >= project.ticksToSamples(n.endTick()));
    QVERIFY(clip->lengthTicks() >= n.endTick());
}

void TestMidi::recorderRecordsIntoHintEvent() {
    Project project;
    Track* track = project.addMidiTrack("Midi 1");
    track->setRecordArmed(true);

    MidiEvent ev;
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(100000);
    auto clip = std::make_shared<MidiClip>();
    clip->setLengthTicks(1000);
    ev.setClip(clip);
    track->addMidiEvent(ev);
    int64_t eventId = track->midiEvents().back().id();

    MidiRecorder rec;
    rec.setTargetHints({ { 0, eventId } });
    rec.captureNote(5000, 72, 90, true);
    rec.captureNote(5250, 72, 90, false);
    rec.pump(project, true, 0, 0);

    // No new event created; notes landed in the hinted event's clip.
    QCOMPARE(track->midiEvents().size(), size_t(1));
    MidiEvent* existing = track->findMidiEvent(eventId);
    QVERIFY(existing);
    MidiClip* existingClip = existing->activeClip().get();
    QVERIFY(existingClip);
    QCOMPARE(existingClip->notes().size(), size_t(1));
    const MidiNote& n = existingClip->notes()[0];
    QCOMPARE(n.pitch, 72);
    QCOMPARE(n.velocity, 90);
    QCOMPARE(n.startTick, project.samplesToTicks(5000));
    QCOMPARE(n.durationTicks, project.samplesToTicks(5250) - project.samplesToTicks(5000));
}

void TestMidi::openFirstDeviceWhenPresent() {
    auto devices = MidiInputManager::enumerateInputDevices();
    if (devices.empty())
        QSKIP("No MIDI input device available on this machine");

    MidiInputManager mgr;
    QVERIFY(!mgr.isActive());
    mgr.open(devices[0].id);
    QVERIFY(mgr.isActive());

    MidiTransportControls controls;
    controls.type = 1;
    controls.play = 110;
    mgr.setTransportControls(controls);

    MidiMessage msg;
    QCOMPARE(mgr.pollNotes(&msg, 1), 0); // no hardware input expected right now
    mgr.closeAll();
    QVERIFY(!mgr.isActive());
}

void TestMidi::cancelReleasesHeldPreviewNotes() {
    Project project;
    Track* track = project.addMidiTrack("Midi 1");
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));
    track->setInstrumentIndex(0);

    AudioEngine engine;
    engine.setProject(&project);

    // Note-on from the MIDI keyboard previews into track 0.
    engine.previewNoteOn(0, 60, 100);
    QCOMPARE(engine.m_previewCount.load(), 1);

    // Focus switch: held notes are marked for release but remain held until
    // the next audio block flushes them.
    engine.cancelPreviewNotes();
    QCOMPARE(engine.m_previewCount.load(), 1);

    // The audio block flush delivers the note-offs and clears the held set.
    engine.injectPreviewMidi();
    QCOMPARE(engine.m_previewCount.load(), 0);
}

QTEST_MAIN(TestMidi)
#include "test_midi.moc"