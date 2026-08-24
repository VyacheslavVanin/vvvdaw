#include "MidiClip.h"
#include <QJsonArray>

int64_t MidiClip::addNote(int pitch, int velocity, int64_t startTick, int64_t durationTicks) {
    MidiNote note;
    note.id = m_nextNoteId++;
    note.pitch = pitch;
    note.velocity = velocity;
    note.startTick = startTick;
    note.durationTicks = durationTicks;
    m_notes.push_back(note);
    ++m_revision;
    return note.id;
}

void MidiClip::importNote(const MidiNote& note) {
    m_notes.push_back(note);
    ++m_revision;
}

bool MidiClip::removeNote(int64_t noteId) {
    auto it = std::remove_if(m_notes.begin(), m_notes.end(),
        [noteId](const MidiNote& n) { return n.id == noteId; });
    if (it == m_notes.end())
        return false;
    m_notes.erase(it, m_notes.end());
    ++m_revision;
    return true;
}

MidiNote* MidiClip::findNote(int64_t noteId) {
    for (auto& n : m_notes)
        if (n.id == noteId) return &n;
    return nullptr;
}

int64_t MidiClip::addControlEvent(MidiControlEvent::Kind kind, uint8_t number,
                                  int value, int64_t startTick) {
    MidiControlEvent ev;
    ev.id = m_nextControlEventId++;
    ev.kind = kind;
    ev.number = number;
    ev.value = value;
    ev.startTick = startTick;
    m_controlEvents.push_back(ev);
    ++m_revision;
    return ev.id;
}

void MidiClip::importControlEvent(const MidiControlEvent& event) {
    m_controlEvents.push_back(event);
    ++m_revision;
}

bool MidiClip::removeControlEvent(int64_t eventId) {
    auto it = std::remove_if(m_controlEvents.begin(), m_controlEvents.end(),
        [eventId](const MidiControlEvent& e) { return e.id == eventId; });
    if (it == m_controlEvents.end())
        return false;
    m_controlEvents.erase(it, m_controlEvents.end());
    ++m_revision;
    return true;
}

MidiControlEvent* MidiClip::findControlEvent(int64_t eventId) {
    for (auto& e : m_controlEvents)
        if (e.id == eventId) return &e;
    return nullptr;
}

int64_t MidiClip::lengthTicks() const {
    int64_t len = m_lengthTicks;
    for (const auto& n : m_notes)
        len = std::max(len, n.endTick());
    for (const auto& e : m_controlEvents)
        len = std::max(len, e.startTick);
    return len;
}

std::shared_ptr<MidiClip> MidiClip::clone() const {
    return std::make_shared<MidiClip>(*this);
}

QJsonObject MidiClip::toJson() const {
    QJsonObject obj;
    obj["ppq"] = kPPQ;
    obj["lengthTicks"] = static_cast<qint64>(m_lengthTicks);
    obj["nextNoteId"] = static_cast<qint64>(m_nextNoteId);
    obj["nextControlEventId"] = static_cast<qint64>(m_nextControlEventId);
    QJsonArray notesArr;
    for (const auto& n : m_notes) {
        QJsonObject nObj;
        nObj["id"] = static_cast<qint64>(n.id);
        nObj["pitch"] = n.pitch;
        nObj["velocity"] = n.velocity;
        nObj["startTick"] = static_cast<qint64>(n.startTick);
        nObj["durationTicks"] = static_cast<qint64>(n.durationTicks);
        notesArr.append(nObj);
    }
    obj["notes"] = notesArr;
    QJsonArray controlArr;
    for (const auto& e : m_controlEvents) {
        QJsonObject eObj;
        eObj["id"] = static_cast<qint64>(e.id);
        eObj["kind"] = static_cast<int>(e.kind);
        eObj["number"] = e.number;
        eObj["value"] = e.value;
        eObj["startTick"] = static_cast<qint64>(e.startTick);
        controlArr.append(eObj);
    }
    obj["controlEvents"] = controlArr;
    return obj;
}

void MidiClip::fromJson(const QJsonObject& obj) {
    m_lengthTicks = static_cast<int64_t>(obj["lengthTicks"].toVariant().toLongLong());
    m_nextNoteId = obj.contains("nextNoteId")
        ? static_cast<int64_t>(obj["nextNoteId"].toVariant().toLongLong())
        : 1;
    m_nextControlEventId = obj.contains("nextControlEventId")
        ? static_cast<int64_t>(obj["nextControlEventId"].toVariant().toLongLong())
        : 1;
    const QJsonArray notesArr = obj["notes"].toArray();
    for (const auto& nVal : notesArr) {
        QJsonObject nObj = nVal.toObject();
        MidiNote note;
        note.id = nObj.contains("id")
            ? static_cast<int64_t>(nObj["id"].toVariant().toLongLong())
            : m_nextNoteId++;
        note.pitch = nObj["pitch"].toInt(60);
        note.velocity = nObj["velocity"].toInt(100);
        note.startTick = static_cast<int64_t>(nObj["startTick"].toVariant().toLongLong());
        note.durationTicks = static_cast<int64_t>(nObj["durationTicks"].toVariant().toLongLong());
        m_notes.push_back(note);
    }
    const QJsonArray controlArr = obj["controlEvents"].toArray();
    for (const auto& eVal : controlArr) {
        QJsonObject eObj = eVal.toObject();
        MidiControlEvent ev;
        ev.id = eObj.contains("id")
            ? static_cast<int64_t>(eObj["id"].toVariant().toLongLong())
            : m_nextControlEventId++;
        ev.kind = static_cast<MidiControlEvent::Kind>(eObj["kind"].toInt(0));
        ev.number = static_cast<uint8_t>(eObj["number"].toInt(0));
        ev.value = eObj["value"].toInt(0);
        ev.startTick = static_cast<int64_t>(eObj["startTick"].toVariant().toLongLong());
        m_controlEvents.push_back(ev);
    }
    ++m_revision;
}
