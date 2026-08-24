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
    void recorderCreatesEventBeforeAnyNote();
    void recorderGrowsEventDuration();
    void recorderBeginWithHintDoesNotDuplicate();
    void recorderCapturesControlEvents();
    void recorderControlEventsIntoHintEvent();
    void messagePredicates();
    void schedulerSendsControlEventsWithChannel();
    void reapplyControlStateOnSeek();
    void openFirstDeviceWhenPresent();
    void cancelReleasesHeldPreviewNotes();
    void previewControlFlushesToInstrument();
    void matchTransportCc();
    void matchTransportNote();
    void matchTransportTypeScoping();
    void matchTransportChannelFilter();
    void matchTransportKind();
    void isLearnableDetection();
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

void TestMidi::recorderCreatesEventBeforeAnyNote() {
    Project project;
    Track* armed = project.addMidiTrack("Midi 1");
    armed->setRecordArmed(true);

    MidiRecorder rec;
    const int64_t recordStart = 10000;

    // First pump begins recording. Even though no note has been played yet, the
    // recording event must exist immediately so the track view shows the clip
    // being recorded from the start.
    QVERIFY(!rec.pump(project, true, 10000, recordStart));

    auto& events = project.tracks()[0].midiEvents();
    QCOMPARE(events.size(), size_t(1));
    QCOMPARE(events[0].startSample(), recordStart);
    QVERIFY(events[0].durationSample() > 0);
}

void TestMidi::recorderGrowsEventDuration() {
    Project project;
    Track* armed = project.addMidiTrack("Midi 1");
    armed->setRecordArmed(true);

    MidiRecorder rec;
    const int64_t recordStart = 0;
    QVERIFY(!rec.pump(project, true, 0, recordStart));
    QVERIFY(!rec.pump(project, true, 100000, recordStart));
    QVERIFY(!rec.pump(project, true, 200000, recordStart));

    // The event's duration grows with the playhead even with no notes recorded.
    auto& events = project.tracks()[0].midiEvents();
    QCOMPARE(events.size(), size_t(1));
    QVERIFY(events[0].durationSample() >= 200000);

    // Stop: the recorded extent is preserved, no snap back to a single PPQ.
    QVERIFY(!rec.pump(project, false, 300000, recordStart));
    QVERIFY(events[0].durationSample() >= 290000);
}

void TestMidi::recorderBeginWithHintDoesNotDuplicate() {
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

    // Beginning recording into an open piano-roll event must not create a
    // duplicate; notes keep landing in the hinted event.
    QVERIFY(!rec.pump(project, true, 0, 0));
    QCOMPARE(track->midiEvents().size(), size_t(1));

    rec.captureNote(5000, 72, 90, true);
    rec.captureNote(5250, 72, 90, false);
    QVERIFY(rec.pump(project, true, 5000, 0));
    QCOMPARE(track->midiEvents().size(), size_t(1));
    QCOMPARE(track->findMidiEvent(eventId)->activeClip()->notes().size(), size_t(1));
}

void TestMidi::recorderCapturesControlEvents() {
    Project project;
    Track* armed = project.addMidiTrack("Midi 1");
    armed->setRecordArmed(true);

    MidiRecorder rec;
    const int64_t recordStart = 0;
    rec.pump(project, true, 0, recordStart);

    // CC1 (mod wheel) to 70, then a pitch bend to center (8192).
    rec.captureControl(5000, 0xB0, 1, 70);
    rec.captureControl(10000, 0xE0, 0x00, 0x40);
    QVERIFY(rec.pump(project, true, 20000, recordStart));

    auto& events = project.tracks()[0].midiEvents();
    QCOMPARE(events.size(), size_t(1));
    MidiClip* clip = events[0].activeClip().get();
    QVERIFY(clip);
    QCOMPARE(clip->controlEvents().size(), size_t(2));

    bool foundCc = false;
    bool foundPb = false;
    for (const auto& e : clip->controlEvents()) {
        if (e.kind == MidiControlEvent::Kind::ControlChange) {
            QCOMPARE(e.number, uint8_t(1));
            QCOMPARE(e.value, 70);
            QCOMPARE(e.startTick, project.samplesToTicks(5000));
            foundCc = true;
        } else {
            QCOMPARE(e.kind, MidiControlEvent::Kind::PitchBend);
            QCOMPARE(e.value, 8192);
            QCOMPARE(e.startTick, project.samplesToTicks(10000));
            foundPb = true;
        }
    }
    QVERIFY(foundCc);
    QVERIFY(foundPb);

    // Stop: the created event's extent covers the control events too.
    QVERIFY(!rec.pump(project, false, 30000, recordStart));
    QVERIFY(clip->lengthTicks() >= project.samplesToTicks(10000));
    QVERIFY(events[0].durationSample() >= project.ticksToSamples(clip->lengthTicks()));
}

void TestMidi::recorderControlEventsIntoHintEvent() {
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
    rec.captureControl(5000, 0xB0, 11, 90); // expression CC11
    QVERIFY(rec.pump(project, true, 0, 0));

    // No new event created; the control event landed in the hinted clip.
    QCOMPARE(track->midiEvents().size(), size_t(1));
    MidiClip* c = track->findMidiEvent(eventId)->activeClip().get();
    QVERIFY(c);
    QCOMPARE(c->controlEvents().size(), size_t(1));
    QCOMPARE(c->controlEvents()[0].number, uint8_t(11));
    QCOMPARE(c->controlEvents()[0].value, 90);
    QCOMPARE(c->controlEvents()[0].startTick, project.samplesToTicks(5000));
}

void TestMidi::messagePredicates() {
    MidiMessage cc;
    cc.status = 0xB0; cc.data1 = 1; cc.data2 = 70;
    QVERIFY(cc.isCc());
    QVERIFY(!cc.isPitchBend());
    QVERIFY(cc.isChannelVoice());
    QVERIFY(cc.hasDiscreteValue());
    QCOMPARE(int(cc.channel()), 0);

    // Pitch bend is a 14-bit pair, not a discrete single-byte control.
    MidiMessage pb;
    pb.status = 0xE0; pb.data1 = 0; pb.data2 = 64;
    QVERIFY(pb.isPitchBend());
    QVERIFY(!pb.isCc());
    QVERIFY(pb.isChannelVoice());
    QVERIFY(!pb.hasDiscreteValue());

    MidiMessage onCh10;
    onCh10.status = 0x99; onCh10.data1 = 60; onCh10.data2 = 100;
    QVERIFY(onCh10.isNoteOn());
    QVERIFY(onCh10.isChannelVoice());
    QCOMPARE(int(onCh10.channel()), 9);

    // System messages are not channel voice.
    MidiMessage sys;
    sys.status = 0xF0;
    QVERIFY(!sys.isChannelVoice());
}

void TestMidi::schedulerSendsControlEventsWithChannel() {
    Project project;
    Instrument inst;
    inst.setName("Synth");
    project.addInstrument(std::move(inst));

    Track* track = project.addMidiTrack("Midi 1");
    track->setInstrumentIndex(0);
    track->setMidiChannel(3);

    auto clip = std::make_shared<MidiClip>();
    clip->addNote(60, 100, 0, 480);
    clip->addControlEvent(MidiControlEvent::Kind::ControlChange, 1, 40, 0);
    clip->addControlEvent(MidiControlEvent::Kind::ControlChange, 1, 100, 480);
    clip->addControlEvent(MidiControlEvent::Kind::PitchBend, 0, 8192, 960);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(200000);
    track->addMidiEvent(ev);

    AudioEngine engine;
    engine.setProject(&project);
    engine.ensureInstrumentMidiBuffers(1);
    engine.m_instrumentMidi[0].clear();

    // Block [0, 512): the note onset and the CC1=40 event at tick 0 land here.
    // Control events are scheduled before notes.
    engine.scheduleMidiTracks(&project, 512, 0);
    QCOMPARE(engine.m_instrumentMidi[0].size(), size_t(2));

    const MidiMessage& cc = engine.m_instrumentMidi[0][0];
    QCOMPARE(int(cc.status), 0xB3); // CC on channel 4 (index 3)
    QCOMPARE(int(cc.data1), 1);
    QCOMPARE(int(cc.data2), 40);
    QCOMPARE(cc.sampleOffset, 0);

    const MidiMessage& note = engine.m_instrumentMidi[0][1];
    QCOMPARE(int(note.status), 0x93); // note on, channel 4
    QCOMPARE(int(note.data1), 60);
    QCOMPARE(int(note.data2), 100);
    QCOMPARE(note.sampleOffset, 0);

    // Next block at tick 480 (sample 12000): CC1=100 lands, and the note's
    // note-off fires at its end. Once delivered, events are not re-sent.
    engine.m_instrumentMidi[0].clear();
    engine.scheduleMidiTracks(&project, 512, 12000);
    QCOMPARE(engine.m_instrumentMidi[0].size(), size_t(2));
    const MidiMessage& cc2 = engine.m_instrumentMidi[0][0];
    QVERIFY(cc2.isCc());
    QCOMPARE(int(cc2.data2), 100);
    QVERIFY(engine.m_instrumentMidi[0][1].isNoteOff());

    // A later block (no onsets inside) sends nothing new.
    engine.m_instrumentMidi[0].clear();
    engine.scheduleMidiTracks(&project, 512, 20000);
    QCOMPARE(engine.m_instrumentMidi[0].size(), size_t(0));
}

void TestMidi::reapplyControlStateOnSeek() {
    Project project;
    Instrument inst;
    inst.setName("Synth");
    project.addInstrument(std::move(inst));

    Track* track = project.addMidiTrack("Midi 1");
    track->setInstrumentIndex(0);
    track->setMidiChannel(3);

    auto clip = std::make_shared<MidiClip>();
    clip->addControlEvent(MidiControlEvent::Kind::ControlChange, 1, 40, 0);
    clip->addControlEvent(MidiControlEvent::Kind::ControlChange, 1, 100, 480);
    clip->addControlEvent(MidiControlEvent::Kind::PitchBend, 0, 8192, 960);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(200000);
    track->addMidiEvent(ev);

    AudioEngine engine;
    engine.setProject(&project);
    engine.ensureInstrumentMidiBuffers(1);
    engine.m_instrumentMidi[0].clear();

    // Seek to sample 15000 (tick 600): the last CC1 value is 100 (at tick 480);
    // pitch bend's first event is at tick 960, so it resets to center.
    engine.reapplyControlState(&project, 15000);
    QCOMPARE(engine.m_instrumentMidi[0].size(), size_t(2));
    bool foundCc100 = false;
    bool foundPbCenter = false;
    for (const auto& m : engine.m_instrumentMidi[0]) {
        if (m.isCc()) {
            QCOMPARE(int(m.status), 0xB3);
            QCOMPARE(int(m.data1), 1);
            QCOMPARE(int(m.data2), 100);
            QCOMPARE(m.sampleOffset, 0);
            foundCc100 = true;
        } else if (m.isPitchBend()) {
            QCOMPARE(int(m.status), 0xE3);
            QCOMPARE(int(m.data1), 0);
            QCOMPARE(int(m.data2), 64); // (8192 >> 7) & 0x7F
            foundPbCenter = true;
        }
    }
    QVERIFY(foundCc100);
    QVERIFY(foundPbCenter);
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

void TestMidi::previewControlFlushesToInstrument() {
    Project project;
    Track* track = project.addMidiTrack("Midi 1");
    track->setMidiChannel(7);
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));
    track->setInstrumentIndex(0);

    AudioEngine engine;
    engine.setProject(&project);
    engine.ensureInstrumentMidiBuffers(1);

    // A mod-wheel move latches a preview control message (CC1 on channel 8).
    engine.previewControl(0, 0xB0, 1, 66);
    QCOMPARE(engine.m_previewControlCount.load(), 1);

    // The next audio block flush delivers it into the instrument buffer.
    engine.m_instrumentMidi[0].clear();
    engine.injectPreviewMidi();
    QCOMPARE(engine.m_previewControlCount.load(), 0);
    QCOMPARE(engine.m_instrumentMidi[0].size(), size_t(1));
    const MidiMessage& m = engine.m_instrumentMidi[0][0];
    QCOMPARE(int(m.status), 0xB7);
    QCOMPARE(int(m.data1), 1);
    QCOMPARE(int(m.data2), 66);
}

void TestMidi::matchTransportCc() {
    MidiTransportControls c;
    c.type = 1;
    c.play = 110; c.record = 111; c.stop = 112;

    MidiMessage msg;
    msg.status = 0xB0; msg.data1 = 110;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::Play);
    msg.data1 = 112;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::Stop);
    msg.data1 = 111;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::Record);
    msg.data1 = 0;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::None);

    // CC value (data2) does not matter.
    msg.data1 = 110; msg.data2 = 0;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::Play);
}

void TestMidi::matchTransportNote() {
    MidiTransportControls c;
    c.type = 2;
    c.play = 114; c.record = 118; c.stop = 117;

    MidiMessage msg;
    msg.status = 0x90; msg.data1 = 114; msg.data2 = 127;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::Play);
    msg.data1 = 117;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::Stop);
    msg.data1 = 118;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::Record);
    msg.data1 = 60;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::None);

    // A note-off (0x90 with velocity 0) never triggers transport.
    msg.data1 = 114; msg.data2 = 0;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::None);
}

void TestMidi::matchTransportTypeScoping() {
    MidiTransportControls c;
    c.type = 1;
    c.play = 110; c.record = 111; c.stop = 112;

    // type == 1 must ignore note messages.
    MidiMessage note;
    note.status = 0x90; note.data1 = 110; note.data2 = 100;
    QCOMPARE(MidiInputManager::matchTransport(note, c), MidiTransportCommand::None);

    // type == 0 disables transport control entirely.
    c.type = 0;
    MidiMessage cc;
    cc.status = 0xB0; cc.data1 = 110;
    QCOMPARE(MidiInputManager::matchTransport(cc, c), MidiTransportCommand::None);
}

void TestMidi::matchTransportChannelFilter() {
    MidiTransportControls c;
    c.type = 1;
    c.channel = 9; // MIDI channel 10
    c.play = 110; c.record = 111; c.stop = 112;

    // CC on channel 9 (0x9 = channel 10): matches.
    MidiMessage msg;
    msg.status = 0xB9; msg.data1 = 110;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::Play);

    // Same controller on a different channel: ignored.
    msg.status = 0xB0; msg.data1 = 110;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::None);

    // Note mapping on channel 9 matches; other channel does not.
    c.type = 2;
    c.play = 114;
    msg.status = 0x99; msg.data1 = 114; msg.data2 = 127;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::Play);
    msg.status = 0x90; msg.data1 = 114; msg.data2 = 127;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::None);

    // channel -1 means any channel.
    c.type = 1;
    c.channel = -1;
    c.play = 110;
    msg.status = 0xB0; msg.data1 = 110;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::Play);
    msg.status = 0xB5; msg.data1 = 110;
    QCOMPARE(MidiInputManager::matchTransport(msg, c), MidiTransportCommand::Play);
}

void TestMidi::matchTransportKind() {
    // A pad that sends a note-off (0x80) or note-on with velocity 0 must be
    // learnable and, once learned with `kind`, trigger transport on the same
    // message family.
    MidiTransportControls c;
    c.type = 2;
    c.kind = 0x80; // learned from a note-off pad press
    c.play = 70;
    c.channel = 9;

    MidiMessage noteOff;
    noteOff.status = 0x89; noteOff.data1 = 70; noteOff.data2 = 0;
    QCOMPARE(MidiInputManager::matchTransport(noteOff, c), MidiTransportCommand::Play);

    // The same pad pressed again sends a normal note-on press: that is a
    // different message family and must not re-trigger.
    MidiMessage noteOn;
    noteOn.status = 0x99; noteOn.data1 = 70; noteOn.data2 = 100;
    QCOMPARE(MidiInputManager::matchTransport(noteOn, c), MidiTransportCommand::None);

    // Poly pressure / program change learned pads also match their own kind.
    MidiTransportControls poly;
    poly.type = 2;
    poly.kind = 0xA0;
    poly.record = 71;
    MidiMessage polyMsg;
    polyMsg.status = 0xA0; polyMsg.data1 = 71; polyMsg.data2 = 100;
    QCOMPARE(MidiInputManager::matchTransport(polyMsg, poly), MidiTransportCommand::Record);

    MidiTransportControls pc;
    pc.type = 2;
    pc.kind = 0xC0;
    pc.stop = 5;
    MidiMessage pcMsg;
    pcMsg.status = 0xC0; pcMsg.data1 = 5;
    QCOMPARE(MidiInputManager::matchTransport(pcMsg, pc), MidiTransportCommand::Stop);
}

void TestMidi::isLearnableDetection() {
    MidiMessage cc;
    cc.status = 0xB0; cc.data1 = 110; cc.data2 = 127;
    QVERIFY(MidiInputManager::isLearnable(cc));
    cc.data2 = 0; // CC release still carries the controller number
    QVERIFY(MidiInputManager::isLearnable(cc));

    MidiMessage noteOn;
    noteOn.status = 0x90; noteOn.data1 = 60; noteOn.data2 = 100;
    QVERIFY(MidiInputManager::isLearnable(noteOn));

    // Pads and buttons often send a release-style message (note-off, or note-on
    // with velocity 0); they must be learnable so those controls can be mapped.
    MidiMessage noteOff;
    noteOff.status = 0x80; noteOff.data1 = 60; noteOff.data2 = 0;
    QVERIFY(MidiInputManager::isLearnable(noteOff));

    MidiMessage noteOnVel0;
    noteOnVel0.status = 0x90; noteOnVel0.data1 = 60; noteOnVel0.data2 = 0;
    QVERIFY(MidiInputManager::isLearnable(noteOnVel0));

    // Poly pressure, program change and channel pressure carry a value in data1.
    MidiMessage poly;
    poly.status = 0xA0; poly.data1 = 70; poly.data2 = 100;
    QVERIFY(MidiInputManager::isLearnable(poly));
    MidiMessage programChange;
    programChange.status = 0xC0; programChange.data1 = 5;
    QVERIFY(MidiInputManager::isLearnable(programChange));
    MidiMessage channelPressure;
    channelPressure.status = 0xD0; channelPressure.data1 = 100;
    QVERIFY(MidiInputManager::isLearnable(channelPressure));

    // Pitch bend is a 14-bit value, not a discrete control; system messages
    // (0xF0+) are not learnable.
    MidiMessage pitchBend;
    pitchBend.status = 0xE0;
    QVERIFY(!MidiInputManager::isLearnable(pitchBend));
    MidiMessage sysEx;
    sysEx.status = 0xF0;
    QVERIFY(!MidiInputManager::isLearnable(sysEx));
}

QTEST_MAIN(TestMidi)
#include "test_midi.moc"