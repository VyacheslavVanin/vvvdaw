#include <QTest>
#include <QApplication>
#include <QJsonArray>
#include <QTemporaryDir>
#include <memory>
#include <vector>
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "model/MidiEvent.h"
#include "model/MidiClip.h"
#include "model/AudioBus.h"
#include "model/Instrument.h"
#include "model/TemplateStore.h"

class TestTemplates : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void builtInNamesAreListed();
    void emptyTemplateHasNoTracksOrInstruments();
    void rockBandTemplateStructure();
    void saveAndLoadUserTemplate();
    void saveTemplateLeavesProjectPathUntouched();
    void saveTemplateRefusesOverwriteWithoutFlag();
    void saveTemplateOverwritesWithFlag();
    void loadTemplateLeavesProjectUnsaved();
    void loadUnknownTemplateFails();
    void deleteUserTemplateRemovesFolder();
    void deleteTemplateRefusesBuiltIn();
    void deleteNonexistentOrInvalidFails();
    void sanitizeName();
private:
    QTemporaryDir* m_tmpDir = nullptr;
};

void TestTemplates::initTestCase() {
    m_tmpDir = new QTemporaryDir;
    QVERIFY(m_tmpDir->isValid());
    TemplateStore::setTemplatesDirOverride(m_tmpDir->path());
    TemplateStore::ensureBuiltInTemplates();
}

void TestTemplates::cleanupTestCase() {
    TemplateStore::setTemplatesDirOverride("");
    delete m_tmpDir;
    m_tmpDir = nullptr;
}

void TestTemplates::builtInNamesAreListed() {
    QStringList names = TemplateStore::listTemplates();
    QVERIFY(names.contains("empty"));
    QVERIFY(names.contains("rock-band"));
}


void TestTemplates::emptyTemplateHasNoTracksOrInstruments() {
    Project p;
    QVERIFY(TemplateStore::loadTemplate(p, "empty"));
    QVERIFY(p.tracks().empty());
    QVERIFY(p.instruments().empty());
    QCOMPARE(p.buses().size(), size_t(2));
    QCOMPARE(p.buses()[0].name(), QString("Master"));
    QCOMPARE(p.buses()[1].name(), QString("Metronome"));
    QVERIFY(p.filePath().isEmpty());
}


void TestTemplates::rockBandTemplateStructure() {
    Project p;
    QVERIFY(TemplateStore::loadTemplate(p, "rock-band"));

    QCOMPARE(p.tracks().size(), size_t(6));
    QCOMPARE(p.buses().size(), size_t(4));
    QCOMPARE(p.buses()[2].name(), QString("Guitars"));
    QCOMPARE(p.buses()[3].name(), QString("Drums"));
    QCOMPARE(p.instruments().size(), size_t(1));
    QCOMPARE(p.instruments()[0].name(), QString("Synth"));

    QStringList expected = {"Solo Guitar", "Rhythm Guitar 1", "Rhythm Guitar 2",
                            "Bass", "Drums", "Synth"};
    for (int i = 0; i < 6; ++i)
        QCOMPARE(p.tracks()[i].name(), expected[i]);

    // Guitars and bass route to the Guitars bus, drums to the Drums bus.
    for (int i = 0; i < 4; ++i)
        QCOMPARE(p.tracks()[i].outputBusIndex(), 2);
    QCOMPARE(p.tracks()[4].outputBusIndex(), 3);

    // The synthesizer is a MIDI track routed to the placeholder instrument.
    QCOMPARE(p.tracks()[5].type(), Track::Type::Midi);
    QCOMPARE(p.tracks()[5].instrumentIndex(), 0);
}


void TestTemplates::saveAndLoadUserTemplate() {
    Project source;
    source.addTrack("Drums");
    AudioBus bus;
    bus.setName("FX");
    source.addBus(std::move(bus));

    QVERIFY(TemplateStore::saveTemplate(source, "myband"));
    QVERIFY(TemplateStore::exists("myband"));
    QVERIFY(TemplateStore::listTemplates().contains("myband"));

    Project loaded;
    QVERIFY(TemplateStore::loadTemplate(loaded, "myband"));
    QCOMPARE(loaded.tracks().size(), size_t(1));
    QCOMPARE(loaded.tracks()[0].name(), QString("Drums"));
    QCOMPARE(loaded.buses().size(), size_t(3));
}


void TestTemplates::saveTemplateLeavesProjectPathUntouched() {
    Project source;
    source.setFilePath("/some/project/project.json");
    QVERIFY(TemplateStore::saveTemplate(source, "keep_path"));
    QCOMPARE(source.filePath(), QString("/some/project/project.json"));
}


void TestTemplates::saveTemplateRefusesOverwriteWithoutFlag() {
    Project source;
    source.addTrack("A");
    QVERIFY(TemplateStore::saveTemplate(source, "no_overwrite"));
    QVERIFY(!TemplateStore::saveTemplate(source, "no_overwrite"));
}


void TestTemplates::saveTemplateOverwritesWithFlag() {
    Project source;
    source.addTrack("A");
    QVERIFY(TemplateStore::saveTemplate(source, "with_overwrite"));
    Project modified;
    modified.addTrack("B");
    QVERIFY(TemplateStore::saveTemplate(modified, "with_overwrite", true));

    Project loaded;
    QVERIFY(TemplateStore::loadTemplate(loaded, "with_overwrite"));
    QCOMPARE(loaded.tracks().size(), size_t(1));
    QCOMPARE(loaded.tracks()[0].name(), QString("B"));
}


void TestTemplates::loadTemplateLeavesProjectUnsaved() {
    Project p;
    QVERIFY(TemplateStore::loadTemplate(p, "rock-band"));
    QVERIFY(p.filePath().isEmpty());
    QCOMPARE(p.name(), QString("Untitled"));
}


void TestTemplates::loadUnknownTemplateFails() {
    Project p;
    QVERIFY(!TemplateStore::loadTemplate(p, "does_not_exist"));
}


void TestTemplates::deleteUserTemplateRemovesFolder() {
    Project source;
    source.addTrack("Drums");
    QVERIFY(TemplateStore::saveTemplate(source, "to_delete"));
    QVERIFY(TemplateStore::exists("to_delete"));
    QVERIFY(TemplateStore::listTemplates().contains("to_delete"));

    QVERIFY(TemplateStore::removeTemplate("to_delete"));
    QVERIFY(!TemplateStore::exists("to_delete"));
    QVERIFY(!TemplateStore::listTemplates().contains("to_delete"));
    QFile file(TemplateStore::templateFilePath("to_delete"));
    QVERIFY(!file.exists());
}

void TestTemplates::deleteTemplateRefusesBuiltIn() {
    QVERIFY(!TemplateStore::removeTemplate("empty"));
    QVERIFY(!TemplateStore::removeTemplate("rock-band"));
    QVERIFY(TemplateStore::exists("empty"));
    QVERIFY(TemplateStore::exists("rock-band"));
}

void TestTemplates::deleteNonexistentOrInvalidFails() {
    QVERIFY(!TemplateStore::removeTemplate("does_not_exist"));
    QVERIFY(!TemplateStore::removeTemplate("../evil/name"));
    QVERIFY(!TemplateStore::removeTemplate("   "));
}


void TestTemplates::sanitizeName() {
    QCOMPARE(TemplateStore::sanitizeName(" My Band "), QString("My Band"));
    QCOMPARE(TemplateStore::sanitizeName("../evil/name"), QString("evil name"));
    QCOMPARE(TemplateStore::sanitizeName("a b"), QString("a b"));
    QVERIFY(TemplateStore::sanitizeName("..").isEmpty());
    QVERIFY(TemplateStore::sanitizeName("   ").isEmpty());
    QVERIFY(TemplateStore::sanitizeName("/").isEmpty());
}


QTEST_MAIN(TestTemplates)
#include "test_model_templates.moc"
