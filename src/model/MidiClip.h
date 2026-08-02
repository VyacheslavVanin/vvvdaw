#pragma once
#include <cstdint>
#include <vector>
#include <QString>
#include <QJsonObject>
#include "MidiNote.h"

class MidiClip {
public:
    static constexpr int kPPQ = 960;

    MidiClip() = default;

    bool isValid() const { return !m_notes.empty() || m_lengthTicks > 0; }

    const std::vector<MidiNote>& notes() const { return m_notes; }
    std::vector<MidiNote>& notes() { return m_notes; }

    int64_t addNote(int pitch, int velocity, int64_t startTick, int64_t durationTicks);
    void importNote(const MidiNote& note);
    bool removeNote(int64_t noteId);
    MidiNote* findNote(int64_t noteId);

    int64_t lengthTicks() const;
    void setLengthTicks(int64_t ticks) { m_lengthTicks = ticks; }
    int64_t revision() const { return m_revision; }
    void bumpRevision() { ++m_revision; }

    int64_t nextNoteId() const { return m_nextNoteId; }
    void setNextNoteId(int64_t id) { m_nextNoteId = id; }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

private:
    std::vector<MidiNote> m_notes;
    int64_t m_lengthTicks = 0;
    int64_t m_nextNoteId = 1;
    int64_t m_revision = 0;
};
