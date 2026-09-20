#pragma once
#include "core/UndoCommand.h"
#include "model/AudioClip.h"
#include <QString>
#include <memory>
#include <vector>

class Project;

// One prepared audio file waiting to become a track: the display name, the
// track channel count derived from the source, and a clip already resampled
// to the project sample rate (and backed by a WAV in the project's audio dir).
struct ImportedAudio {
    QString name;
    int channels = 2;
    std::shared_ptr<AudioClip> clip;
};

// Load `path` (any libsndfile-supported format, including MP3), resample it to
// the project's sample rate, and save it as a WAV in the project's audio
// directory. Returns the resulting clip (loaded from the saved WAV) or nullptr
// on failure, with `error` set to a human-readable reason when non-null.
std::shared_ptr<AudioClip> prepareImportedClip(const Project& project,
                                               const QString& path,
                                               QString* error = nullptr);

// Append one audio track per prepared file, each carrying a single event that
// starts at sample 0 and plays the clip at its native length. Undo removes the
// appended tracks.
class ImportTracksCommand : public UndoCommand {
public:
    ImportTracksCommand(Project& project, std::vector<ImportedAudio> items);
    void execute() override;
    void undo() override;
    int id() const override { return 150; }
private:
    Project& m_project;
    std::vector<ImportedAudio> m_items;
    int m_firstIndex = -1;
    int m_addedCount = 0;
};
