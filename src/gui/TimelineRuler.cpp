#include "TimelineRuler.h"
#include "core/TimeUtils.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QMenu>
#include <cmath>

TimelineRuler::TimelineRuler(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(28);
    setMouseTracking(true);
}

int64_t TimelineRuler::sampleAtX(int x) const {
    int64_t sample = m_scrollOffset + static_cast<int64_t>(x / m_pixelsPerSample);
    if (sample < 0) sample = 0;
    if (m_snapToGrid)
        sample = TimeUtils::snapSample(sample, m_snapUnit);
    return sample;
}

TimelineRuler::DragHandle TimelineRuler::handleAtPos(int x) const {
    auto handleHit = [&](int64_t sample) -> bool {
        if (sample < 0) return false;
        int sx = static_cast<int>((sample - m_scrollOffset) * m_pixelsPerSample);
        return std::abs(x - sx) < 6;
    };

    if (m_rrStart >= 0 && handleHit(m_rrStart)) return DragHandle::RRStart;
    if (m_rrEnd >= 0 && handleHit(m_rrEnd)) return DragHandle::RREnd;
    if (m_loopStart >= 0 && handleHit(m_loopStart)) return DragHandle::LoopStart;
    if (m_loopEnd >= 0 && handleHit(m_loopEnd)) return DragHandle::LoopEnd;
    return DragHandle::None;
}

void TimelineRuler::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_pixelsPerSample > 0) {
        DragHandle handle = handleAtPos(static_cast<int>(event->position().x()));
        if (handle != DragHandle::None) {
            m_dragging = true;
            m_dragHandle = handle;
            m_dragStartMouseX = static_cast<int>(event->position().x());
            int64_t* target = nullptr;
            if (handle == DragHandle::LoopStart) target = &m_loopStart;
            else if (handle == DragHandle::LoopEnd) target = &m_loopEnd;
            else if (handle == DragHandle::RRStart) target = &m_rrStart;
            else if (handle == DragHandle::RREnd) target = &m_rrEnd;
            if (target) m_dragStartValue = *target;
            return;
        }
        emit playheadClicked(sampleAtX(static_cast<int>(event->position().x())));
    }
    QWidget::mousePressEvent(event);
}

void TimelineRuler::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging) {
        int dx = static_cast<int>(event->position().x()) - m_dragStartMouseX;
        int64_t delta = static_cast<int64_t>(dx / m_pixelsPerSample);
        if (delta == 0) return;
        int64_t newVal = m_dragStartValue + delta;
        if (newVal < 0) newVal = 0;

        if (m_snapToGrid)
            newVal = TimeUtils::snapSample(newVal, m_snapUnit);

        int64_t* target = nullptr;
        int64_t* other = nullptr;
        if (m_dragHandle == DragHandle::LoopStart) {
            target = &m_loopStart; other = &m_loopEnd;
            if (other && newVal >= *other) newVal = *other - vvvdaw::MinLoopGapSamples;
        } else if (m_dragHandle == DragHandle::LoopEnd) {
            target = &m_loopEnd; other = &m_loopStart;
            if (other && newVal <= *other) newVal = *other + vvvdaw::MinLoopGapSamples;
        } else if (m_dragHandle == DragHandle::RRStart) {
            target = &m_rrStart; other = &m_rrEnd;
            if (other && newVal >= *other) newVal = *other - vvvdaw::MinLoopGapSamples;
        } else if (m_dragHandle == DragHandle::RREnd) {
            target = &m_rrEnd; other = &m_rrStart;
            if (other && newVal <= *other) newVal = *other + vvvdaw::MinLoopGapSamples;
        }
        if (target) *target = newVal;
        update();
        return;
    }
    // Cursor feedback for handles
    DragHandle h = handleAtPos(static_cast<int>(event->position().x()));
    setCursor(h != DragHandle::None ? Qt::SplitHCursor : Qt::ArrowCursor);
}

void TimelineRuler::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        DragHandle h = m_dragHandle;
        m_dragHandle = DragHandle::None;
        setCursor(Qt::ArrowCursor);
        // Emit change signal
        if (h == DragHandle::LoopStart || h == DragHandle::LoopEnd) {
            if (m_loopStart >= 0 && m_loopEnd > m_loopStart)
                emit loopChanged(m_loopStart, m_loopEnd);
            else
                emit loopRemoved();
        } else if (h == DragHandle::RRStart || h == DragHandle::RREnd) {
            if (m_rrStart >= 0 && m_rrEnd > m_rrStart)
                emit recordRegionChanged(m_rrStart, m_rrEnd);
            else
                emit recordRegionRemoved();
        }
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TimelineRuler::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    int64_t sample = sampleAtX(static_cast<int>(event->pos().x()));

    auto addRangeAction = [&](bool active, int64_t& start, int64_t& end,
                              const QString& setLabel, const QString& removeLabel,
                              auto clearFn, auto createdFn, auto removedFn)
    {
        if (active) {
            QAction* act = menu.addAction(removeLabel);
            connect(act, &QAction::triggered, this, [this, clearFn, removedFn] {
                (this->*clearFn)();
                (this->*removedFn)();
            });
        } else {
            QAction* act = menu.addAction(setLabel);
            connect(act, &QAction::triggered, this, [this, sample, &start, &end, createdFn] {
                start = sample;
                end = sample + static_cast<int64_t>(m_snapUnit * 4);
                if (m_snapToGrid)
                    end = TimeUtils::snapSample(end, m_snapUnit);
                update();
                (this->*createdFn)(start, end);
            });
        }
    };

    addRangeAction(m_loopStart >= 0 && m_loopEnd > m_loopStart,
                   m_loopStart, m_loopEnd,
                   "Set Loop Here", "Remove Loop",
                   &TimelineRuler::clearLoop,
                   &TimelineRuler::loopCreated,
                   &TimelineRuler::loopRemoved);

    menu.addSeparator();

    addRangeAction(m_rrStart >= 0 && m_rrEnd > m_rrStart,
                   m_rrStart, m_rrEnd,
                   "Set Record Region Here", "Remove Record Region",
                   &TimelineRuler::clearRecordRegion,
                   &TimelineRuler::recordRegionCreated,
                   &TimelineRuler::recordRegionRemoved);

    menu.exec(event->globalPos());
}

void TimelineRuler::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#2a2a2a"));

    double tickInterval = vvvdaw::TickIntervalSamples;
    if (tickInterval * m_pixelsPerSample < 60) {
        tickInterval *= 4;
    }

    int64_t startSample = m_scrollOffset;
    int64_t endSample = m_scrollOffset + static_cast<int64_t>(width() / m_pixelsPerSample);

    // Draw record region range bar (behind everything)
    if (m_rrStart >= 0 && m_rrEnd > m_rrStart) {
        drawRange(painter, m_rrStart, m_rrEnd,
                  QColor(180, 40, 40, 60), QColor(200, 60, 60), QColor(200, 60, 60),
                  "R");
    }

    // Draw loop range bar
    if (m_loopStart >= 0 && m_loopEnd > m_loopStart) {
        drawRange(painter, m_loopStart, m_loopEnd,
                  QColor(40, 120, 40, 50), QColor(60, 160, 60), QColor(60, 160, 60),
                  "L");
    }

    // Ticks
    int64_t firstTick = (startSample / static_cast<int64_t>(tickInterval)) * static_cast<int64_t>(tickInterval);

    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);

    // Minor ticks
    painter.setPen(QPen(QColor("#555"), 1));
    for (int64_t s = firstTick; s <= endSample; s += static_cast<int64_t>(tickInterval)) {
        int x = static_cast<int>((s - m_scrollOffset) * m_pixelsPerSample);
        if (x < 0 || x > width()) continue;
        for (int i = 1; i < 4; ++i) {
            int mx = x + static_cast<int>((tickInterval / 4) * i * m_pixelsPerSample);
            if (mx >= 0 && mx <= width())
                painter.drawLine(mx, 20, mx, 28);
        }
    }

    // Major ticks + labels
    painter.setPen(QPen(QColor("#aaa"), 1));
    for (int64_t s = firstTick; s <= endSample; s += static_cast<int64_t>(tickInterval)) {
        int x = static_cast<int>((s - m_scrollOffset) * m_pixelsPerSample);
        if (x < 0 || x > width()) continue;

        painter.drawLine(x, 16, x, 28);

        int seconds = static_cast<int>(s / vvvdaw::DefaultSampleRate);
        int minutes = seconds / 60;
        int secs = seconds % 60;
        QString label = QString("%1:%2")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
        painter.setPen(QColor("#ccc"));
        painter.drawText(x + 3, 12, label);
        painter.setPen(QPen(QColor("#aaa"), 1));
    }

    // Playhead marker
    if (m_playheadPos >= 0) {
        int phx = static_cast<int>((m_playheadPos - m_scrollOffset) * m_pixelsPerSample);
        if (phx >= 0 && phx <= width()) {
            painter.setPen(QPen(QColor("#ff4444"), 2));
            painter.drawLine(phx, 0, phx, 28);
            QPolygonF triangle;
            triangle << QPointF(phx - 4, 0) << QPointF(phx + 4, 0) << QPointF(phx, 6);
            painter.setBrush(QColor("#ff4444"));
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(triangle);
        }
    }
}

void TimelineRuler::drawRange(QPainter& painter, int64_t rangeStart, int64_t rangeEnd,
                               const QColor& fill, const QColor& border, const QColor& handleColor,
                               const QString& label)
{
    int x1 = static_cast<int>((rangeStart - m_scrollOffset) * m_pixelsPerSample);
    int x2 = static_cast<int>((rangeEnd - m_scrollOffset) * m_pixelsPerSample);
    if (x2 < 0 || x1 > width()) return;
    int xStart = std::max(0, x1);
    int xEnd = std::min(width(), x2);

    // Fill
    painter.fillRect(xStart, 0, xEnd - xStart, height(), fill);

    // Border lines
    painter.setPen(QPen(border, 2));
    if (x1 >= 0 && x1 <= width()) painter.drawLine(x1, 0, x1, height());
    if (x2 >= 0 && x2 <= width()) painter.drawLine(x2, 0, x2, height());

    // Handles (small squares at top)
    auto drawHandle = [&](int x) {
        painter.fillRect(x - 3, 0, 6, 8, handleColor);
        painter.setPen(Qt::NoPen);
    };
    if (x1 >= 0 && x1 <= width()) drawHandle(x1);
    if (x2 >= 0 && x2 <= width()) drawHandle(x2);

    // Label
    if (xStart < xEnd - 20) {
        painter.setPen(border);
        painter.drawText(xStart + 4, 10, label);
    }
}
