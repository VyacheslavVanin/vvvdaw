#include "TemplateStore.h"
#include "Project.h"
#include "Track.h"
#include "AudioBus.h"
#include "Instrument.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

QString TemplateStore::s_templatesDirOverride;

void TemplateStore::setTemplatesDirOverride(const QString& dir) {
    s_templatesDirOverride = dir;
}

QString TemplateStore::templatesDir() {
    QString base = s_templatesDirOverride.isEmpty()
                       ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                       : s_templatesDirOverride;
    return base + "/templates";
}

QString TemplateStore::templateFilePath(const QString& name) {
    return templatesDir() + "/" + name + "/project.json";
}

bool TemplateStore::isBuiltIn(const QString& name) {
    return name == "empty" || name == "rock-band";
}

bool TemplateStore::exists(const QString& name) {
    return isBuiltIn(name) || QFile::exists(templateFilePath(name));
}

QStringList TemplateStore::listTemplates() {
    QStringList names = {"empty", "rock-band"};
    QDir dir(templatesDir());
    QStringList userNames;
    for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (QFile::exists(templateFilePath(entry)) && !names.contains(entry))
            userNames.append(entry);
    }
    names += userNames;
    return names;
}

void TemplateStore::ensureBuiltInTemplates() {
    for (const QString& name : listTemplates())
        if (isBuiltIn(name) && !QFile::exists(templateFilePath(name)))
            writeBuiltIn(name);
}

Project TemplateStore::createBuiltIn(const QString& name) {
    Project project;
    if (name == "empty") {
        project.setName("Empty");
        return project;
    }

    project.setName("Rock Band");

    AudioBus guitars;
    guitars.setName("Guitars");
    guitars.setOutputBusIndex(0);
    AudioBus drums;
    drums.setName("Drums");
    drums.setOutputBusIndex(0);
    project.addBus(std::move(guitars));
    project.addBus(std::move(drums));

    Track* solo = project.addTrack("Solo Guitar");
    solo->setOutputBusIndex(2);
    Track* rhythm1 = project.addTrack("Rhythm Guitar 1");
    rhythm1->setOutputBusIndex(2);
    Track* rhythm2 = project.addTrack("Rhythm Guitar 2");
    rhythm2->setOutputBusIndex(2);
    Track* bass = project.addTrack("Bass");
    bass->setOutputBusIndex(2);
    Track* drumsTrack = project.addTrack("Drums");
    drumsTrack->setOutputBusIndex(3);

    Track* synth = project.addMidiTrack("Synth");
    synth->setInstrumentIndex(0);
    Instrument synthInst;
    synthInst.setName("Synth");
    project.addInstrument(std::move(synthInst));

    return project;
}

bool TemplateStore::loadTemplate(Project& project, const QString& name) {
    if (sanitizeName(name).isEmpty())
        return false;
    if (isBuiltIn(name) && !QFile::exists(templateFilePath(name)))
        writeBuiltIn(name);
    QString path = templateFilePath(name);
    if (!QFile::exists(path))
        return false;
    if (!project.load(path))
        return false;
    // Opening a template is like starting a fresh project: it is not yet
    // saved anywhere, so a normal save prompts for a location.
    project.setFilePath({});
    project.setName("Untitled");
    return true;
}

bool TemplateStore::saveTemplate(Project& project, const QString& name,
                                 bool overwrite) {
    if (sanitizeName(name).isEmpty())
        return false;
    if (exists(name) && !overwrite)
        return false;
    QString path = templateFilePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QString originalPath = project.filePath();
    bool ok = project.save(path);
    // The project stays whatever it was (typically unsaved); the template is
    // just a copy and must not become the project's own save target.
    project.setFilePath(originalPath);
    return ok;
}

QString TemplateStore::sanitizeName(const QString& raw) {
    QString s = raw.trimmed();
    s.replace('\\', ' ');
    s.replace('/', ' ');
    s.replace(QStringLiteral(".."), QStringLiteral(""));
    s = s.simplified();
    while (s.startsWith('.'))
        s = s.mid(1).trimmed();
    if (s.isEmpty() || s == "." || s == "..")
        return {};
    return s;
}

QString TemplateStore::templateDescription(const QString& name) {
    if (name == "empty")
        return "Blank project without tracks or instruments";
    if (name == "rock-band")
        return "Solo guitar, two rhythm guitars, bass, drums and a synthesizer";
    return {};
}

bool TemplateStore::writeBuiltIn(const QString& name) {
    QString path = templateFilePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    Project project = createBuiltIn(name);
    return project.save(path);
}
