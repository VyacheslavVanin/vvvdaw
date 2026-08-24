#include "ControlEventEditorWidget.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/MidiEvent.h"
#include "core/UndoStack.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

ControlEventEditorWidget::ControlEventEditorWidget(Project& project, UndoStack& undo,
                                                   int trackIndex, int64_t eventId,
                                                   QWidget* parent)
    : QWidget(parent)
    , m_project(project)
    , m_undo(undo)
    , m_trackIndex(trackIndex)
    , m_eventId(eventId) {
    setMouseTracking(true);
    setMinimumHeight(kLaneHeight);
    setMinimumWidth(kKeysWidth + 60);
}

QSize ControlEventEditorWidget::sizeHint() const {
    MidiClip* c = clip();
    int64_t len = c ? c->lengthTicks() : 0;
    int contentW = kKeysWidth + 40
        + static_cast<int>(std::max<int64_t>(len, MidiClip::kPPQ * 4) * m_pixelsPerTick);
    return QSize(contentW, kLaneHeight);
}

QSize ControlEventEditorWidget::minimumSizeHint() const {
    return QSize(kKeysWidth + 60, kLaneHeight);
}

bool ControlEventEditorWidget::reload() {
    updateGeometry();
    update();
    return true;
}

void ControlEventEditorWidget::setPixelsPerTick(double p) {
    m_pixelsPerTick = p;
    updateGeometry();
    update();
}

void ControlEventEditorWidget::setSnapDiv(int div) {
    if (div < 1) div = 1;
    m_snapDiv = div;
    update();
}

void ControlEventEditorWidget::setLane(MidiControlEvent::Kind kind, uint8_t number) {
    m_kind = kind;
    m_number = number;
    update();
}

int ControlEventEditorWidget::laneMax() const {
    return m_kind == MidiControlEvent::Kind::PitchBend ? 16383 : 127;
}

MidiClip* ControlEventEditorWidget::clip() const {
    MidiEvent* ev = currentEvent();
    if (!ev) return nullptr;
    return ev->activeClip().get();
}

MidiEvent* ControlEventEditorWidget::currentEvent() const {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return nullptr;
    return m_project.tracks()[m_trackIndex].findMidiEvent(m_eventId);
}

int ControlEventEditorWidget::tickToX(int64_t tick) const {
    return kKeysWidth + static_cast<int>(tick * m_pixelsPerTick);
}

int64_t ControlEventEditorWidget::xToTick(int x) const {
    return static_cast<int64_t>((x - kKeysWidth) / m_pixelsPerTick);
}

bool ControlEventEditorWidget::snapEnabled() const {
    return m_project.snapToGrid();
}

int64_t ControlEventEditorWidget::snapTickFloor(int64_t tick) const {
    if (!snapEnabled()) return tick;
    int64_t unit = MidiClip::kPPQ / m_snapDiv;
    if (unit < 1) unit = 1;
    return static_cast<int64_t>(std::floor(static_cast<double>(tick) / unit) * unit);
}

int ControlEventEditorWidget::valueFromY(int y) const {
    double frac = static_cast<double>(y) / kLaneHeight;
    int max = laneMax();
    int v = max - static_cast<int>(std::lround(frac * max));
    return std::clamp(v, 0, max);
}

int ControlEventEditorWidget::yFromValue(int value) const {
    double frac = static_cast<double>(value) / laneMax();
    return static_cast<int>(std::lround((1.0 - frac) * (kLaneHeight - 2)));
}

MidiControlEvent* ControlEventEditorWidget::eventAtTick(int64_t tick) const {
    MidiClip* c = clip();
    if (!c) return nullptr;
    for (auto& e : c->controlEvents()) {
        if (e.kind == m_kind && e.number == m_number && e.startTick == tick)
            return &e;
    }
    return nullptr;
}

int64_t ControlEventEditorWidget::nearestEventTick(int x) const {
    MidiClip* c = clip();
    if (!c) return -1;
    const int64_t unit = std::max<int64_t>(1, MidiClip::kPPQ / m_snapDiv);
    int64_t bestTick = -1;
    int bestDist = std::numeric_limits<int>::max();
    for (const auto& e : c->controlEvents()) {
        if (e.kind != m_kind || e.number != m_number)
            continue;
        int dist = std::abs(tickToX(e.startTick) - x);
        if (dist < bestDist && dist <= std::max(6, static_cast<int>(unit * m_pixelsPerTick))) {
            bestDist = dist;
            bestTick = e.startTick;
        }
    }
    return bestTick;
}

void ControlEventEditorWidget::paintTo(int x, int y, int64_t lastTick, int lastValue) {
    int64_t curTick = snapTickFloor(xToTick(x));
    int curValue = valueFromY(y);
    if (curTick < 0) curTick = 0;

    auto record = [&](int64_t tick, int value) {
        MidiControlEvent* existing = eventAtTick(tick);
        auto it = m_changeIndexByTick.find(tick);
        if (it != m_changeIndexByTick.end()) {
            m_changes[it->second].newValue = value;
            m_changes[it->second].newStartTick = tick;
            return;
        }
        ControlEventChange ch;
        ch.kind = m_kind;
        ch.number = m_number;
        if (existing) {
            ch.op = ControlEventChange::Op::Update;
            ch.controlEventId = existing->id;
            ch.oldValue = existing->value;
            ch.oldStartTick = existing->startTick;
        } else {
            ch.op = ControlEventChange::Op::Add;
        }
        ch.newValue = value;
        ch.newStartTick = tick;
        m_changeIndexByTick[tick] = m_changes.size();
        m_changes.push_back(ch);
    };

    if (lastTick < 0) {
        record(curTick, curValue);
        return;
    }
    if (curTick == lastTick) {
        if (curValue != lastValue)
            record(curTick, curValue);
        return;
    }

    const int64_t unit = std::max<int64_t>(1, MidiClip::kPPQ / m_snapDiv);
    const int64_t dir = curTick > lastTick ? 1 : -1;
    const int64_t span = std::max<int64_t>(1, std::abs(curTick - lastTick));
    for (int64_t t = lastTick + dir * unit; dir * (t - lastTick) < dir * (curTick - lastTick);
         t += dir * unit) {
        double frac = static_cast<double>(t - lastTick) / span;
        int v = static_cast<int>(std::lround(lastValue + (curValue - lastValue) * frac));
        record(t, std::clamp(v, 0, laneMax()));
    }
    record(curTick, curValue);
}

void ControlEventEditorWidget::beginDrag(const QPoint& pos) {
    m_dragging = true;
    m_changes.clear();
    m_changeIndexByTick.clear();
    m_lastTick = -1;
    m_lastValue = 0;
    grabMouse();
    m_lastTick = static_cast<int>(snapTickFloor(xToTick(pos.x())));
    m_lastValue = valueFromY(pos.y());
    paintTo(pos.x(), pos.y(), -1, 0);
    update();
}

void ControlEventEditorWidget::updateDrag(const QPoint& pos) {
    if (!m_dragging) return;
    int64_t newTick = snapTickFloor(xToTick(pos.x()));
    int newValue = valueFromY(pos.y());
    paintTo(pos.x(), pos.y(), m_lastTick, m_lastValue);
    m_lastTick = static_cast<int>(newTick);
    m_lastValue = newValue;
    update();
}

void ControlEventEditorWidget::endDrag() {
    if (!m_dragging) return;
    m_dragging = false;
    releaseMouse(); // no-op when no grab is held
    if (!m_changes.empty()) {
        m_undo.execute(std::make_unique<EditControlEventsCommand>(
            m_project, m_trackIndex, m_eventId, std::move(m_changes)));
        emit controlEventsChanged();
    }
    m_changes.clear();
    m_changeIndexByTick.clear();
}

void ControlEventEditorWidget::removeAt(const QPoint& pos) {
    int64_t tick = nearestEventTick(pos.x());
    if (tick < 0) return;
    MidiControlEvent* existing = eventAtTick(tick);
    if (!existing) return;
    ControlEventChange ch;
    ch.op = ControlEventChange::Op::Remove;
    ch.kind = m_kind;
    ch.number = m_number;
    ch.controlEventId = existing->id;
    ch.oldValue = existing->value;
    ch.oldStartTick = existing->startTick;
    std::vector<ControlEventChange> changes;
    changes.push_back(ch);
    m_undo.execute(std::make_unique<EditControlEventsCommand>(
        m_project, m_trackIndex, m_eventId, std::move(changes)));
    emit controlEventsChanged();
}

void ControlEventEditorWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#232323"));
    painter.setPen(QPen(QColor("#555"), 1));
    painter.drawLine(0, 0, width(), 0);

    painter.fillRect(0, 0, kKeysWidth, height(), QColor("#1b1b1b"));
    QFont laneFont = painter.font();
    laneFont.setPixelSize(8);
    painter.setFont(laneFont);
    painter.setPen(QColor("#888"));
    const QString label = m_kind == MidiControlEvent::Kind::PitchBend
        ? QStringLiteral("Pitch")
        : QStringLiteral("CC%1").arg(m_number);
    painter.drawText(2, kLaneHeight / 2 - 2, label);

    MidiClip* c = clip();
    if (!c) return;

    // Automation polyline + points for the selected lane.
    std::vector<QPoint> pts;
    for (const auto& e : c->controlEvents()) {
        if (e.kind != m_kind || e.number != m_number)
            continue;
        pts.emplace_back(tickToX(e.startTick), yFromValue(e.value));
    }
    if (pts.size() > 1) {
        painter.setPen(QPen(QColor("#4d94d4"), 1));
        painter.drawPolyline(pts.data(), static_cast<int>(pts.size()));
    }
    for (const auto& p : pts) {
        painter.setPen(QPen(QColor("#123b59"), 1));
        painter.setBrush(QColor("#66ccff"));
        painter.drawEllipse(p, 3, 3);
    }
    if (m_kind == MidiControlEvent::Kind::PitchBend) {
        painter.setPen(QPen(QColor("#4a4a4a"), 1, Qt::DashLine));
        painter.drawLine(kKeysWidth, yFromValue(8192), width(), yFromValue(8192));
    }

    painter.setPen(QPen(QColor("#444"), 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

void ControlEventEditorWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        removeAt(QPoint(static_cast<int>(event->position().x()),
                        static_cast<int>(event->position().y())));
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    beginDrag(QPoint(static_cast<int>(event->position().x()),
                     static_cast<int>(event->position().y())));
    event->accept();
}

void ControlEventEditorWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging) {
        updateDrag(QPoint(static_cast<int>(event->position().x()),
                          static_cast<int>(event->position().y())));
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ControlEventEditorWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        endDrag();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}