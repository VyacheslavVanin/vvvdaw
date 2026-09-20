#include "ImportCommands.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace {

// Filesystem-safe stem derived from a source file name (used in the WAV name).
QString sanitizedStem(const QString& filePath) {
    QString stem = QFileInfo(filePath).completeBaseName();
    for (QChar& c : stem) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('-'))
            c = QLatin1Char('_');
    }
    if (stem.isEmpty())
        stem = QStringLiteral("audio");
    return stem;
}

} // namespace

std::shared_ptr<AudioClip> prepareImportedClip(const Project& project,
                                               const QString& path,
                                               QString* error) {
    auto setError = [error](const QString& message) {
        if (error) *error = message;
    };

    if (path.isEmpty()) {
        setError(QStringLiteral("empty file path"));
        return nullptr;
    }

    auto source = std::make_shared<AudioClip>(path);
    if (!source->isValid()) {
        setError(QStringLiteral("unsupported or unreadable audio file"));
        return nullptr;
    }

    const QString audioDir = project.audioDirectory();
    if (!QDir().mkpath(audioDir)) {
        setError(QStringLiteral("cannot create audio directory: %1").arg(audioDir));
        return nullptr;
    }

    const QString outPath = QStringLiteral("%1/import_%2_%3.wav")
        .arg(audioDir, sanitizedStem(path))
        .arg(QDateTime::currentMSecsSinceEpoch());

    if (!source->saveResampledToFile(outPath, project.sampleRate())) {
        setError(QStringLiteral("cannot write resampled audio to %1").arg(outPath));
        return nullptr;
    }

    auto clip = std::make_shared<AudioClip>(outPath);
    if (!clip->isValid()) {
        setError(QStringLiteral("cannot reload resampled audio from %1").arg(outPath));
        return nullptr;
    }
    clip->setFilePath(outPath);
    return clip;
}

ImportTracksCommand::ImportTracksCommand(Project& project, std::vector<ImportedAudio> items)
    : m_project(project), m_items(std::move(items)) {}

void ImportTracksCommand::execute() {
    m_firstIndex = static_cast<int>(m_project.tracks().size());
    m_addedCount = 0;
    for (const ImportedAudio& item : m_items) {
        if (!item.clip || !item.clip->isValid())
            continue;

        const int channels = std::clamp(item.channels, 1, 2);
        Track* track = m_project.addTrack(item.name, channels);

        AudioEvent event;
        event.setClip(item.clip);
        event.setStartSample(0);
        event.setOffsetSample(0);
        event.setDurationSample(static_cast<int64_t>(item.clip->frameCount()));
        event.setSourceFrames(static_cast<int64_t>(item.clip->frameCount()));
        track->addEvent(event);
        ++m_addedCount;
    }
}

void ImportTracksCommand::undo() {
    if (m_firstIndex < 0)
        return;
    for (int i = 0; i < m_addedCount; ++i) {
        if (m_firstIndex >= static_cast<int>(m_project.tracks().size()))
            break;
        m_project.removeTrack(m_firstIndex);
    }
}
