#include "PianoRollWidget.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/MidiEvent.h"
#include "commands/MidiCommands.h"
#include "core/UndoStack.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <algorithm>
#include <cmath>

PianoRollWidget::PianoRollWidget(Project& project, UndoStack& undo, int trackIndex, int64_t eventId,
                                 QWidget* parent)
    : QWidget(parent)
    , m_project(project)
    , m_undo(undo)
    , m_trackIndex(trackIndex)
    , m_eventId(eventId)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumSize(kKeysWidth + 200, 128 * kRowHeight + 4);
}

MidiClip* PianoRollWidget::clip() const {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return nullptr;
    MidiEvent* event = m_project.tracks()[m_trackIndex].findMidiEvent(m_eventId);
    if (!event) return nullptr;
    return event->activeClip().get();
}

bool PianoRollWidget::reload() {
    MidiClip* c = clip();
    if (!c) {
        m_selectedNoteId = -1;
        return false;
    }
    m_selectedNoteId = -1;
    update();
    return true;
}

int64_t PianoRollWidget::xToTick(int x) const {
    return static_cast<int64_t>((x - kKeysWidth) / m_pixelsPerTick);
}

int PianoRollWidget::tickToX(int64_t tick) const {
    return kKeysWidth + static_cast<int>(tick * m_pixelsPerTick);
}

int PianoRollWidget::pitchToY(int pitch) const {
    return (127 - pitch) * kRowHeight;
}

int PianoRollWidget::yToPitch(int y) const {
    return 127 - y / kRowHeight;
}

int64_t PianoRollWidget::snapTick(int64_t tick) const {
    int64_t unit = MidiClip::kPPQ / m_snapDiv;
    if (unit < 1) unit = 1;
    return static_cast<int64_t>(std::round(static_cast<double>(tick) / unit) * unit);
}

PianoRollWidget::NoteRect PianoRollWidget::noteRectAt(int x, int y) const {
    MidiClip* c = clip();
    if (!c) return {};
    for (auto it = c->notes().rbegin(); it != c->notes().rend(); ++it) {
        const auto& note = *it;
        int nx = tickToX(note.startTick);
        int nw = std::max(4, static_cast<int>(note.durationTicks * m_pixelsPerTick));
        int ny = pitchToY(note.pitch);
        if (x >= nx && x <= nx + nw && y >= ny && y < ny + kRowHeight) {
            NoteRect nr;
            nr.noteId = note.id;
            nr.x = nx;
            nr.y = ny;
            nr.w = nw;
            nr.h = kRowHeight;
            nr.rightEdge = (x >= nx + nw - kEdgeWidth);
            return nr;
        }
    }
    return {};
}

void PianoRollWidget::setSnapDiv(int div) {
    if (div < 1) div = 1;
    m_snapDiv = div;
    update();
}

void PianoRollWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#1e1e1e"));

    MidiClip* c = clip();
    if (!c) {
        painter.setPen(QColor("#888"));
        painter.drawText(20, 30, "No MIDI data");
        return;
    }

    int64_t clipLen = c->lengthTicks();

    // Horizontal pitch rows
    painter.setPen(QPen(QColor("#2a2a2a"), 1));
    for (int pitch = 0; pitch <= 127; ++pitch) {
        int y = pitchToY(pitch);
        if (pitch % 12 == 0)
            painter.fillRect(kKeysWidth, y, width() - kKeysWidth, kRowHeight, QColor("#242a34"));
        painter.drawLine(kKeysWidth, y, width(), y);
    }

    // Vertical beat/bar grid
    int64_t beatTicks = MidiClip::kPPQ;
    int64_t barTicks = beatTicks * m_project.timeSigNum();
    int64_t snapTicks = std::max<int64_t>(1, beatTicks / m_snapDiv);
    if (snapTicks * m_pixelsPerTick < 6)
        snapTicks = beatTicks;
    for (int64_t t = 0; t <= clipLen + beatTicks; t += snapTicks) {
        int x = tickToX(t);
        if (x < kKeysWidth) continue;
        bool isBar = (t % barTicks == 0);
        bool isBeat = (t % beatTicks == 0);
        QColor color = isBar ? QColor("#4a4a5a")
                     : (isBeat ? QColor("#3a3a3a") : QColor("#2d2d2d"));
        painter.setPen(QPen(color, isBar ? 2 : 1));
        painter.drawLine(x, 0, x, height());
    }

    // Piano keys
    int keyWidth = kKeysWidth;
    QFont keyFont = painter.font();
    keyFont.setPixelSize(7);
    painter.setFont(keyFont);
    for (int pitch = 0; pitch <= 127; ++pitch) {
        int y = pitchToY(pitch);
        bool black = (pitch % 12 == 1 || pitch % 12 == 3 || pitch % 12 == 6
                      || pitch % 12 == 8 || pitch % 12 == 10);
        painter.fillRect(0, y, keyWidth, kRowHeight,
                         black ? QColor("#2a2a2a") : QColor("#3a3a3a"));
        if (pitch % 12 == 0) {
            painter.setPen(QColor("#888"));
            painter.drawText(2, y + kRowHeight - 2, QString::number(pitch / 12 - 1));
        }
        painter.setPen(QColor("#222"));
        painter.drawLine(0, y, keyWidth, y);
    }
    painter.fillRect(keyWidth - 2, 0, 2, height(), QColor("#444"));

    // Notes
    for (const auto& note : c->notes()) {
        int x = tickToX(note.startTick);
        int w = std::max(3, static_cast<int>(note.durationTicks * m_pixelsPerTick));
        int y = pitchToY(note.pitch);
        bool selected = (note.id == m_selectedNoteId);
        QColor fill("#4d94d4");
        if (note.velocity < 64) fill = QColor("#3a6b9e");
        if (selected) fill = QColor("#66ccff");
        painter.setPen(QPen(selected ? QColor("#ffdd66") : QColor("#123b59"), 1));
        painter.setBrush(fill);
        painter.drawRect(x, y, w, kRowHeight - 1);
    }
}

void PianoRollWidget::wheelEvent(QWheelEvent* event) {
    int delta = static_cast<int>(event->angleDelta().y());
    if (event->modifiers() & Qt::ControlModifier && delta != 0) {
        double factor = (delta > 0) ? 1.2 : 1.0 / 1.2;
        m_pixelsPerTick = std::clamp(m_pixelsPerTick * factor, 0.004, 2.0);
        update();
    } else if ((event->modifiers() & Qt::ShiftModifier) && delta != 0) {
        if (m_selectedNoteId >= 0) {
            MidiClip* c = clip();
            MidiNote* note = c ? c->findNote(m_selectedNoteId) : nullptr;
            if (note) {
                int oldVel = note->velocity;
                int newVel = std::clamp(oldVel + (delta > 0 ? 8 : -8), 1, 127);
                if (newVel != oldVel) {
                    note->velocity = newVel;
                    c->bumpRevision();
                    m_undo.execute(std::make_unique<SetNoteVelocityCommand>(
                        m_project, m_trackIndex, m_eventId, note->id, oldVel, newVel));
                    update();
                }
            }
        }
    }
    event->accept();
}

void PianoRollWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || event->position().x() < kKeysWidth) {
        QWidget::mousePressEvent(event);
        return;
    }

    NoteRect nr = noteRectAt(static_cast<int>(event->position().x()),
                             static_cast<int>(event->position().y()));
    m_dragMode = DragMode::None;
    m_dragNoteId = -1;

    if (nr.noteId >= 0) {
        MidiClip* c = clip();
        MidiNote* note = c ? c->findNote(nr.noteId) : nullptr;
        if (!note) return;
        m_selectedNoteId = nr.noteId;
        m_dragNoteId = nr.noteId;
        m_dragOrigPitch = note->pitch;
        m_dragOrigStartTick = note->startTick;
        m_dragOrigDuration = static_cast<int>(note->durationTicks);
        m_dragMouseX = static_cast<int>(event->position().x());
        m_dragMouseY = static_cast<int>(event->position().y());
        m_dragMode = nr.rightEdge ? DragMode::Resize : DragMode::Move;
        update();
        return;
    }

    m_selectedNoteId = -1;
    update();
    QWidget::mousePressEvent(event);
}

void PianoRollWidget::mouseMoveEvent(QMouseEvent* event) {
    int mx = static_cast<int>(event->position().x());
    int my = static_cast<int>(event->position().y());

    if (m_dragMode != DragMode::None && m_dragNoteId >= 0) {
        MidiClip* c = clip();
        MidiNote* note = c ? c->findNote(m_dragNoteId) : nullptr;
        if (!note) return;

        if (m_dragMode == DragMode::Move) {
            int dx = mx - m_dragMouseX;
            int dy = my - m_dragMouseY;
            int newPitch = std::clamp(m_dragOrigPitch - dy / kRowHeight, 0, 127);
            int64_t newStart = snapTick(m_dragOrigStartTick + xToTick(dx));
            if (newStart < 0) newStart = 0;
            if (newPitch != note->pitch || newStart != note->startTick) {
                note->pitch = newPitch;
                note->startTick = newStart;
                c->bumpRevision();
                m_undo.execute(std::make_unique<MoveNoteCommand>(
                    m_project, m_trackIndex, m_eventId, note->id,
                    m_dragOrigPitch, m_dragOrigStartTick, newPitch, newStart));
                update();
            }
        } else if (m_dragMode == DragMode::Resize) {
            int dx = mx - m_dragMouseX;
            int64_t newDur = snapTick(m_dragOrigDuration + xToTick(dx));
            if (newDur < MidiClip::kPPQ / 16) newDur = MidiClip::kPPQ / 16;
            if (newDur != note->durationTicks) {
                note->durationTicks = newDur;
                c->bumpRevision();
                m_undo.execute(std::make_unique<ResizeNoteCommand>(
                    m_project, m_trackIndex, m_eventId, note->id,
                    m_dragOrigDuration, newDur));
                update();
            }
        }
        return;
    }

    NoteRect nr = noteRectAt(mx, my);
    setCursor(nr.noteId >= 0
              ? (nr.rightEdge ? Qt::SizeHorCursor : Qt::SizeAllCursor)
              : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void PianoRollWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragMode = DragMode::None;
        m_dragNoteId = -1;
    }
    QWidget::mouseReleaseEvent(event);
}

void PianoRollWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || event->position().x() < kKeysWidth) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    MidiClip* c = clip();
    if (!c) return;

    int mx = static_cast<int>(event->position().x());
    int my = static_cast<int>(event->position().y());
    if (noteRectAt(mx, my).noteId >= 0) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    int pitch = std::clamp(yToPitch(my), 0, 127);
    int64_t start = snapTick(xToTick(mx));
    if (start < 0) start = 0;
    int64_t dur = MidiClip::kPPQ / m_snapDiv;
    if (dur < MidiClip::kPPQ / 16) dur = MidiClip::kPPQ / 16;

    auto cmd = std::make_unique<AddNoteCommand>(
        m_project, m_trackIndex, m_eventId, pitch, m_lastVelocity, start, dur);
    m_undo.execute(std::move(cmd));
    if (auto* added = dynamic_cast<AddNoteCommand*>(m_undo.topCommand()))
        m_selectedNoteId = added->createdNoteId();
    update();
}

void PianoRollWidget::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && m_selectedNoteId >= 0) {
        MidiClip* c = clip();
        if (c && c->findNote(m_selectedNoteId)) {
            m_undo.execute(std::make_unique<RemoveNoteCommand>(
                m_project, m_trackIndex, m_eventId, m_selectedNoteId));
            m_selectedNoteId = -1;
            update();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}
