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
#include <utility>

namespace {
constexpr int64_t kMinDurationTicks = MidiClip::kPPQ / 16;
constexpr int kVelocityStep = 8;

bool isBlackKey(int pitch) {
    int m = pitch % 12;
    return m == 1 || m == 3 || m == 6 || m == 8 || m == 10;
}
} // namespace

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
    setMinimumSize(kKeysWidth + 60, 128 * kRowHeight + 4);
}

QSize PianoRollWidget::sizeHint() const {
    MidiClip* c = clip();
    int64_t len = c ? c->lengthTicks() : 0;
    int contentW = kKeysWidth + 40 + static_cast<int>(std::max<int64_t>(len, MidiClip::kPPQ * 4) * m_pixelsPerTick);
    return QSize(contentW, 128 * kRowHeight + 4);
}

QSize PianoRollWidget::minimumSizeHint() const {
    return QSize(kKeysWidth + 60, 128 * kRowHeight + 4);
}

MidiClip* PianoRollWidget::clip() const {
    MidiEvent* ev = currentEvent();
    if (!ev) return nullptr;
    return ev->activeClip().get();
}

MidiEvent* PianoRollWidget::currentEvent() const {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return nullptr;
    return m_project.tracks()[m_trackIndex].findMidiEvent(m_eventId);
}

bool PianoRollWidget::reload() {
    if (!clip()) {
        clearDragState();
        m_selectedNoteIds.clear();
        return false;
    }
    clearDragState();
    m_selectedNoteIds.clear();
    updateGeometry();
    update();
    return true;
}

int64_t PianoRollWidget::xToTick(int x) const {
    return static_cast<int64_t>((x - kKeysWidth) / m_pixelsPerTick);
}

int64_t PianoRollWidget::clickToTimelineSample(int x) const {
    MidiEvent* ev = currentEvent();
    if (!ev) return -1;
    int64_t tick = snapEnabled() ? snapTick(xToTick(x)) : xToTick(x);
    int64_t offsetTicks = m_project.samplesToTicks(ev->offsetSample());
    int64_t sample = ev->startSample() + m_project.ticksToSamples(tick - offsetTicks);
    return std::max<int64_t>(0, sample);
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

bool PianoRollWidget::snapEnabled() const {
    return m_project.snapToGrid();
}

int64_t PianoRollWidget::snapTick(int64_t tick) const {
    if (!snapEnabled()) return tick;
    int64_t unit = MidiClip::kPPQ / m_snapDiv;
    if (unit < 1) unit = 1;
    return static_cast<int64_t>(std::round(static_cast<double>(tick) / unit) * unit);
}

int64_t PianoRollWidget::snapTickFloor(int64_t tick) const {
    if (!snapEnabled()) return tick;
    int64_t unit = MidiClip::kPPQ / m_snapDiv;
    if (unit < 1) unit = 1;
    return static_cast<int64_t>(std::floor(static_cast<double>(tick) / unit) * unit);
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

void PianoRollWidget::collectNoteRects(std::vector<NoteRect>& out) const {
    MidiClip* c = clip();
    if (!c) return;
    for (const auto& note : c->notes()) {
        NoteRect nr;
        nr.noteId = note.id;
        nr.x = tickToX(note.startTick);
        nr.w = std::max(3, static_cast<int>(note.durationTicks * m_pixelsPerTick));
        nr.y = pitchToY(note.pitch);
        nr.h = kRowHeight;
        out.push_back(nr);
    }
}

void PianoRollWidget::selectNotesInRect(const QRect& rect, bool add) {
    std::vector<NoteRect> rects;
    collectNoteRects(rects);
    if (!add)
        m_selectedNoteIds.clear();
    for (const auto& nr : rects) {
        QRect noteRect(nr.x, nr.y, nr.w, nr.h);
        if (noteRect.intersects(rect))
            m_selectedNoteIds.insert(nr.noteId);
    }
    update();
}

void PianoRollWidget::clearDragState() {
    m_dragMode = DragMode::None;
    m_dragOriginals.clear();
    m_rubberBanding = false;
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

    // Horizontal pitch rows: white keys lighter, black keys darker, but with
    // muted contrast so the grid stays readable without visual noise.
    for (int pitch = 0; pitch <= 127; ++pitch) {
        int y = pitchToY(pitch);
        QColor rowColor;
        if (pitch % 12 == 0)
            rowColor = QColor("#3f4450");   // C / octave reference (tinted)
        else if (isBlackKey(pitch))
            rowColor = QColor("#262626");   // black keys
        else
            rowColor = QColor("#3a3a3a");   // white keys
        painter.fillRect(kKeysWidth, y, width() - kKeysWidth, kRowHeight, rowColor);
        QColor lineColor = isBlackKey(pitch) ? QColor("#333333") : QColor("#4a4a4a");
        painter.setPen(QPen(lineColor, 1));
        painter.drawLine(kKeysWidth, y, width(), y);
    }

    // Vertical beat/bar grid (subtle, visible on both row shades)
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
        QColor color = isBar ? QColor("#5a5a5a")
                     : (isBeat ? QColor("#4a4a4a") : QColor("#3c3c3c"));
        painter.setPen(QPen(color, isBar ? 2 : 1));
        painter.drawLine(x, 0, x, height());
    }

    // Piano keys: white keys lighter, black keys darker (muted contrast)
    QFont keyFont = painter.font();
    keyFont.setPixelSize(7);
    painter.setFont(keyFont);
    for (int pitch = 0; pitch <= 127; ++pitch) {
        int y = pitchToY(pitch);
        bool black = isBlackKey(pitch);
        painter.fillRect(0, y, kKeysWidth, kRowHeight,
                         black ? QColor("#282828") : QColor("#3f3f3f"));
        if (pitch % 12 == 0) {
            painter.setPen(black ? QColor("#aaa") : QColor("#aaa"));
            painter.drawText(2, y + kRowHeight - 2, QString::number(pitch / 12 - 1));
        }
        painter.setPen(QColor("#333"));
        painter.drawLine(0, y, kKeysWidth, y);
    }
    painter.fillRect(kKeysWidth - 2, 0, 2, height(), QColor("#444"));

    // Notes
    for (const auto& note : c->notes()) {
        int x = tickToX(note.startTick);
        int w = std::max(3, static_cast<int>(note.durationTicks * m_pixelsPerTick));
        int y = pitchToY(note.pitch);
        bool selected = m_selectedNoteIds.count(note.id) > 0;
        QColor fill("#4d94d4");
        if (note.velocity < 64) fill = QColor("#3a6b9e");
        if (selected) fill = QColor("#66ccff");
        painter.setPen(QPen(selected ? QColor("#ffdd66") : QColor("#123b59"), 1));
        painter.setBrush(fill);
        painter.drawRect(x, y, w, kRowHeight - 1);
    }

    // Rubber-band selection rectangle
    if (m_rubberBanding) {
        QRect selRect = QRect(m_rubberStart, m_rubberCurrent).normalized();
        painter.fillRect(selRect, QColor(102, 204, 255, 40));
        painter.setPen(QPen(QColor("#66ccff"), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selRect);
    }

    // Playhead line (timeline position mapped into the clip, only when inside)
    if (m_playheadSample >= 0) {
        MidiEvent* ev = currentEvent();
        if (ev) {
            double spt = m_project.samplesPerTick();
            if (spt > 0) {
                double offsetTicks = static_cast<double>(ev->offsetSample()) / spt;
                double tick = (static_cast<double>(m_playheadSample - ev->startSample()) / spt)
                              + offsetTicks;
                if (tick >= 0.0 && tick <= clipLen) {
                    int x = tickToX(static_cast<int64_t>(std::lround(tick)));
                    painter.setPen(QPen(QColor("#ff4444"), 2));
                    painter.drawLine(x, 0, x, height());
                }
            }
        }
    }
}

void PianoRollWidget::wheelEvent(QWheelEvent* event) {
    int delta = static_cast<int>(event->angleDelta().y());
    if (event->modifiers() & Qt::ControlModifier && delta != 0) {
        double factor = (delta > 0) ? 1.2 : 1.0 / 1.2;
        m_pixelsPerTick = std::clamp(m_pixelsPerTick * factor, 0.004, 2.0);
        updateGeometry();
        update();
    } else if ((event->modifiers() & Qt::ShiftModifier) && delta != 0
               && !m_selectedNoteIds.empty()) {
        MidiClip* c = clip();
        if (!c) return;
        int step = (delta > 0) ? kVelocityStep : -kVelocityStep;
        std::vector<NoteVelocityChange> changes;
        for (int64_t id : m_selectedNoteIds) {
            MidiNote* note = c->findNote(id);
            if (!note) continue;
            NoteVelocityChange ch;
            ch.noteId = id;
            ch.oldVelocity = note->velocity;
            ch.newVelocity = std::clamp(note->velocity + step, 1, 127);
            if (ch.newVelocity != ch.oldVelocity) {
                note->velocity = ch.newVelocity;
                changes.push_back(ch);
            }
        }
        if (!changes.empty()) {
            c->bumpRevision();
            m_undo.execute(std::make_unique<SetNotesVelocityCommand>(
                m_project, m_trackIndex, m_eventId, std::move(changes)));
            updateGeometry();
            update();
        }
    }
    event->accept();
}

void PianoRollWidget::duplicateSelection() {
    MidiClip* c = clip();
    if (!c) return;
    std::vector<int64_t> ids(m_selectedNoteIds.begin(), m_selectedNoteIds.end());
    if (ids.empty()) return;
    auto cmd = std::make_unique<DuplicateNotesCommand>(m_project, m_trackIndex, m_eventId, ids);
    m_undo.execute(std::move(cmd));
    m_selectedNoteIds.clear();
    if (auto* dup = dynamic_cast<DuplicateNotesCommand*>(m_undo.topCommand()))
        for (int64_t id : dup->createdNoteIds())
            m_selectedNoteIds.insert(id);
    updateGeometry();
    update();
}

void PianoRollWidget::beginNoteDrag(int noteId, const QPoint& pos, bool resize) {
    MidiClip* c = clip();
    if (!c) return;
    m_dragMode = resize ? DragMode::Resize : DragMode::Move;
    m_dragOriginals.clear();
    for (int64_t id : m_selectedNoteIds) {
        MidiNote* note = c->findNote(id);
        if (!note) continue;
        NoteOrig orig;
        orig.noteId = id;
        orig.pitch = note->pitch;
        orig.startTick = note->startTick;
        orig.durationTicks = note->durationTicks;
        m_dragOriginals.push_back(orig);
    }
    m_dragMouseX = pos.x();
    m_dragMouseY = pos.y();
}

void PianoRollWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || event->position().x() < kKeysWidth) {
        QWidget::mousePressEvent(event);
        return;
    }

    QPoint pos(static_cast<int>(event->position().x()), static_cast<int>(event->position().y()));
    NoteRect nr = noteRectAt(pos.x(), pos.y());
    bool ctrl = (event->modifiers() & Qt::ControlModifier);
    bool shift = (event->modifiers() & Qt::ShiftModifier);

    clearDragState();

    if (nr.noteId >= 0) {
        bool alreadySelected = m_selectedNoteIds.count(nr.noteId) > 0;

        if (ctrl) {
            // Duplicate the selection (or the clicked note) and move the copies.
            if (!alreadySelected) {
                m_selectedNoteIds.clear();
                m_selectedNoteIds.insert(nr.noteId);
            }
            duplicateSelection();
            beginNoteDrag(nr.noteId, pos, false);
            update();
            return;
        }

        if (shift) {
            if (alreadySelected) {
                m_selectedNoteIds.erase(nr.noteId);
            } else {
                m_selectedNoteIds.insert(nr.noteId);
                beginNoteDrag(nr.noteId, pos, nr.rightEdge);
            }
            update();
            return;
        }

        if (!alreadySelected) {
            m_selectedNoteIds.clear();
            m_selectedNoteIds.insert(nr.noteId);
        }
        beginNoteDrag(nr.noteId, pos, nr.rightEdge);
        update();
        return;
    }

    // Empty area: start rubber-band selection.
    m_rubberBanding = true;
    m_rubberStart = pos;
    m_rubberCurrent = pos;
    if (!shift)
        m_selectedNoteIds.clear();
    update();
}

void PianoRollWidget::mouseMoveEvent(QMouseEvent* event) {
    int mx = static_cast<int>(event->position().x());
    int my = static_cast<int>(event->position().y());

    if (m_rubberBanding) {
        m_rubberCurrent = QPoint(mx, my);
        update();
        return;
    }

    if (m_dragMode != DragMode::None && !m_dragOriginals.empty()) {
        MidiClip* c = clip();
        if (!c) return;

        int dx = mx - m_dragMouseX;
        int dy = my - m_dragMouseY;
        double tickDelta = static_cast<double>(dx) / m_pixelsPerTick;

        if (m_dragMode == DragMode::Move) {
            int pitchDelta = -dy / kRowHeight;
            std::vector<NoteMoveChange> changes;
            for (const auto& orig : m_dragOriginals) {
                MidiNote* note = c->findNote(orig.noteId);
                if (!note) continue;
                int newPitch = std::clamp(orig.pitch + pitchDelta, 0, 127);
                int64_t newStart = std::max<int64_t>(0, snapTick(
                    orig.startTick + static_cast<int64_t>(std::lround(tickDelta))));
                if (newPitch == note->pitch && newStart == note->startTick)
                    continue;
                note->pitch = newPitch;
                note->startTick = newStart;
                NoteMoveChange ch;
                ch.noteId = orig.noteId;
                ch.oldPitch = orig.pitch;
                ch.oldStartTick = orig.startTick;
                ch.newPitch = newPitch;
                ch.newStartTick = newStart;
                changes.push_back(ch);
            }
            if (!changes.empty()) {
                c->bumpRevision();
                m_undo.execute(std::make_unique<MoveNotesCommand>(
                    m_project, m_trackIndex, m_eventId, std::move(changes)));
                updateGeometry();
                update();
            }
        } else { // Resize
            std::vector<NoteResizeChange> changes;
            for (const auto& orig : m_dragOriginals) {
                MidiNote* note = c->findNote(orig.noteId);
                if (!note) continue;
                int64_t newDur = std::max<int64_t>(kMinDurationTicks, snapTick(
                    orig.durationTicks + static_cast<int64_t>(std::lround(tickDelta))));
                if (newDur == note->durationTicks)
                    continue;
                note->durationTicks = newDur;
                NoteResizeChange ch;
                ch.noteId = orig.noteId;
                ch.oldDuration = orig.durationTicks;
                ch.newDuration = newDur;
                changes.push_back(ch);
            }
            if (!changes.empty()) {
                c->bumpRevision();
                m_undo.execute(std::make_unique<ResizeNotesCommand>(
                    m_project, m_trackIndex, m_eventId, std::move(changes)));
                updateGeometry();
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
        if (m_rubberBanding) {
            m_rubberBanding = false;
            bool isClick = (m_rubberCurrent - m_rubberStart).manhattanLength() < 3;
            bool ctrl = (event->modifiers() & Qt::ControlModifier);
            if (isClick && ctrl) {
                // Ctrl+click on empty area adds a note.
                addNoteAt(m_rubberStart);
            } else if (isClick) {
                int64_t sample = clickToTimelineSample(m_rubberStart.x());
                if (sample >= 0)
                    emit playheadSetRequested(sample);
            } else {
                QRect selRect = QRect(m_rubberStart, m_rubberCurrent).normalized();
                bool shift = (event->modifiers() & Qt::ShiftModifier);
                selectNotesInRect(selRect, shift);
            }
        }
        clearDragState();
        unsetCursor();
    }
    QWidget::mouseReleaseEvent(event);
}

void PianoRollWidget::addNoteAt(const QPoint& pos) {
    MidiClip* c = clip();
    if (!c || pos.x() < kKeysWidth) return;

    // Floor to the cell under the pointer; the note lands in the row the
    // pointer is actually in.
    int pitch = std::clamp(yToPitch(pos.y()), 0, 127);
    int64_t start = snapTickFloor(xToTick(pos.x()));
    if (start < 0) start = 0;
    int64_t dur = MidiClip::kPPQ / m_snapDiv;
    if (dur < kMinDurationTicks) dur = kMinDurationTicks;

    auto cmd = std::make_unique<AddNoteCommand>(
        m_project, m_trackIndex, m_eventId, pitch, m_lastVelocity, start, dur);
    m_undo.execute(std::move(cmd));
    if (auto* added = dynamic_cast<AddNoteCommand*>(m_undo.topCommand())) {
        m_selectedNoteIds.clear();
        m_selectedNoteIds.insert(added->createdNoteId());
    }
    updateGeometry();
    update();
}

void PianoRollWidget::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && !m_selectedNoteIds.empty()) {
        MidiClip* c = clip();
        if (c) {
            std::vector<int64_t> ids(m_selectedNoteIds.begin(), m_selectedNoteIds.end());
            m_undo.execute(std::make_unique<RemoveNotesCommand>(
                m_project, m_trackIndex, m_eventId, ids));
            m_selectedNoteIds.clear();
            updateGeometry();
            update();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}
