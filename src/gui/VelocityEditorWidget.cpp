#include "VelocityEditorWidget.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/MidiEvent.h"
#include "core/UndoStack.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

VelocityEditorWidget::VelocityEditorWidget(Project& project, UndoStack& undo,
                                           int trackIndex, int64_t eventId, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
    , m_undo(undo)
    , m_trackIndex(trackIndex)
    , m_eventId(eventId) {
    setMouseTracking(true);
    setMinimumHeight(kLaneHeight);
}

QSize VelocityEditorWidget::sizeHint() const {
    MidiClip* c = clip();
    int64_t len = c ? c->lengthTicks() : 0;
    int contentW = kKeysWidth + 40
        + static_cast<int>(std::max<int64_t>(len, MidiClip::kPPQ * 4) * m_pixelsPerTick);
    return QSize(contentW, kLaneHeight);
}

QSize VelocityEditorWidget::minimumSizeHint() const {
    return QSize(kKeysWidth + 60, kLaneHeight);
}

bool VelocityEditorWidget::reload() {
    updateGeometry();
    update();
    return true;
}

void VelocityEditorWidget::setPixelsPerTick(double p) {
    m_pixelsPerTick = p;
    updateGeometry();
    update();
}

void VelocityEditorWidget::setSelection(const std::set<int64_t>& ids) {
    m_selectedNoteIds = ids;
    update();
}

MidiClip* VelocityEditorWidget::clip() const {
    MidiEvent* ev = currentEvent();
    if (!ev) return nullptr;
    return ev->activeClip().get();
}

MidiEvent* VelocityEditorWidget::currentEvent() const {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return nullptr;
    return m_project.tracks()[m_trackIndex].findMidiEvent(m_eventId);
}

int VelocityEditorWidget::tickToX(int64_t tick) const {
    return kKeysWidth + static_cast<int>(tick * m_pixelsPerTick);
}

void VelocityEditorWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#232323"));
    painter.setPen(QPen(QColor("#555"), 1));
    painter.drawLine(0, 0, width(), 0);

    int laneBottom = height();

    painter.fillRect(0, 0, kKeysWidth, height(), QColor("#1b1b1b"));
    QFont laneFont = painter.font();
    laneFont.setPixelSize(8);
    painter.setFont(laneFont);
    painter.setPen(QColor("#888"));
    painter.drawText(2, kLaneHeight / 2 - 2, "Vel");

    MidiClip* c = clip();
    if (c) {
        const auto& notes = c->notes();
        const auto drawBar = [this, &painter, laneBottom](const MidiNote& note) {
            int bx = tickToX(note.startTick);
            int bh = std::max(2, static_cast<int>(note.velocity / 127.0 * (kLaneHeight - 8)));
            bool selected = m_selectedNoteIds.count(note.id) > 0;
            painter.fillRect(bx, laneBottom - 4 - bh, kBarWidth, bh,
                             selected ? QColor("#66ccff") : QColor("#3d7bbf"));
        };
        for (const auto& note : notes)
            if (m_selectedNoteIds.count(note.id) == 0)
                drawBar(note);
        for (const auto& note : notes)
            if (m_selectedNoteIds.count(note.id) > 0)
                drawBar(note);
    }
    painter.setPen(QPen(QColor("#444"), 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

void VelocityEditorWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    QPoint pos(static_cast<int>(event->position().x()),
               static_cast<int>(event->position().y()));
    beginVelocityDrag(pos);
}

void VelocityEditorWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_velDragging) {
        QPoint pos(static_cast<int>(event->position().x()),
                   static_cast<int>(event->position().y()));
        updateVelocityDrag(pos);
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void VelocityEditorWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_velDragging) {
        endVelocityDrag();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

int VelocityEditorWidget::velocityFromY(int y) const {
    double frac = static_cast<double>(y) / kLaneHeight;
    int v = 127 - static_cast<int>(std::lround(frac * 126.0));
    return std::clamp(v, 1, 127);
}

    int64_t VelocityEditorWidget::velocityBarAt(int x) const {
    MidiClip* c = clip();
    if (!c) return -1;
    for (const auto& note : c->notes()) {
        int bx = tickToX(note.startTick);
        if (x >= bx && x <= bx + kBarWidth)
            return note.id;
    }
    return -1;
}

std::vector<int64_t> VelocityEditorWidget::notesInXRange(int x0, int x1) const {
    std::vector<int64_t> ids;
    MidiClip* c = clip();
    if (!c) return ids;
    int lo = std::min(x0, x1);
    int hi = std::max(x0, x1);
    for (const auto& note : c->notes()) {
        int bx = tickToX(note.startTick);
        if (bx + kBarWidth >= lo && bx <= hi)
            ids.push_back(note.id);
    }
    return ids;
}

void VelocityEditorWidget::beginVelocityDrag(const QPoint& pos) {
    MidiClip* c = clip();
    if (!c) return;

    m_velDragging = true;
    m_velDragValue = velocityFromY(pos.y());
    m_velLastX = pos.x();
    m_velChanges.clear();
    m_velActiveNote = -1;
    m_velSelectionMode = !m_selectedNoteIds.empty();
    grabMouse();

    if (m_velSelectionMode) {
        std::set<int64_t> affected = m_selectedNoteIds;
        int64_t underCursor = velocityBarAt(pos.x());
        if (underCursor >= 0)
            affected.insert(underCursor);
        for (int64_t id : affected) {
            MidiNote* note = c->findNote(id);
            if (!note) continue;
            NoteVelocityChange ch;
            ch.noteId = id;
            ch.oldVelocity = note->velocity;
            note->velocity = m_velDragValue;
            ch.newVelocity = m_velDragValue;
            m_velChanges.push_back(ch);
        }
    } else {
        // Paint mode: the bar under the cursor becomes the active bar and
        // follows the pointer; bars the sweep later crosses are fixed.
        int64_t underCursor = velocityBarAt(pos.x());
        if (underCursor >= 0) {
            MidiNote* note = c->findNote(underCursor);
            if (note) {
                NoteVelocityChange ch;
                ch.noteId = underCursor;
                ch.oldVelocity = note->velocity;
                note->velocity = m_velDragValue;
                ch.newVelocity = m_velDragValue;
                m_velChanges.push_back(ch);
            }
            m_velActiveNote = underCursor;
        }
    }
    update();
}

void VelocityEditorWidget::updateVelocityDrag(const QPoint& pos) {
    MidiClip* c = clip();
    if (!c || !m_velDragging) return;

    int v = velocityFromY(pos.y());
    m_velDragValue = v;

    if (m_velSelectionMode) {
        // Selection mode: every affected note follows the pointer height.
        for (auto& ch : m_velChanges) {
            MidiNote* note = c->findNote(ch.noteId);
            if (!note) continue;
            ch.newVelocity = v;
            note->velocity = v;
        }
    } else {
        // Paint mode: bars jumped over by the sweep are painted once at the
        // value they had when crossed; the bar under the cursor keeps
        // following the pointer until the sweep leaves it.
        int64_t newActive = velocityBarAt(pos.x());

        for (int64_t id : notesInXRange(m_velLastX, pos.x())) {
            if (id == newActive) continue;
            bool known = false;
            for (const auto& ch : m_velChanges) {
                if (ch.noteId == id) { known = true; break; }
            }
            if (known) continue;
            MidiNote* note = c->findNote(id);
            if (!note) continue;
            NoteVelocityChange ch;
            ch.noteId = id;
            ch.oldVelocity = note->velocity;
            note->velocity = v;
            ch.newVelocity = v;
            m_velChanges.push_back(ch);
        }

        if (newActive != m_velActiveNote) {
            if (newActive >= 0) {
                bool known = false;
                for (const auto& ch : m_velChanges) {
                    if (ch.noteId == newActive) { known = true; break; }
                }
                if (!known) {
                    MidiNote* note = c->findNote(newActive);
                    if (note) {
                        NoteVelocityChange ch;
                        ch.noteId = newActive;
                        ch.oldVelocity = note->velocity;
                        note->velocity = v;
                        ch.newVelocity = v;
                        m_velChanges.push_back(ch);
                    }
                }
            }
            m_velActiveNote = newActive;
        }

        if (m_velActiveNote >= 0) {
            for (auto& ch : m_velChanges) {
                if (ch.noteId == m_velActiveNote) {
                    MidiNote* note = c->findNote(ch.noteId);
                    if (note) {
                        ch.newVelocity = v;
                        note->velocity = v;
                    }
                    break;
                }
            }
        }
    }
    m_velLastX = pos.x();
    update();
}

void VelocityEditorWidget::endVelocityDrag() {
    m_velDragging = false;
    m_velActiveNote = -1;
    releaseMouse(); // no-op when no grab is held
    unsetCursor();

    m_velChanges.erase(
        std::remove_if(m_velChanges.begin(), m_velChanges.end(),
                       [](const NoteVelocityChange& ch) {
                           return ch.newVelocity == ch.oldVelocity;
                       }),
        m_velChanges.end());

    if (!m_velChanges.empty()) {
        MidiClip* c = clip();
        if (c) {
            c->bumpRevision();
            m_undo.execute(std::make_unique<SetNotesVelocityCommand>(
                m_project, m_trackIndex, m_eventId, std::move(m_velChanges)));
        }
    }
    m_velChanges.clear();
}
