#include "RangeRulerWidget.h"
#include "core/TimeUtils.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QMenu>
#include <cmath>

RangeRulerWidget::RangeRulerWidget(int fixedHeight, QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(fixedHeight);
    setMouseTracking(true);
}

RangeRulerWidget::DragHandle RangeRulerWidget::handleAtPos(int x) const {
    auto handleHit = [&](int64_t sample) -> bool {
        if (sample < 0) return false;
        return std::abs(x - sampleToX(sample)) < 6;
    };

    if (m_rrStart >= 0 && handleHit(m_rrStart)) return DragHandle::RRStart;
    if (m_rrEnd >= 0 && handleHit(m_rrEnd)) return DragHandle::RREnd;
    if (m_loopStart >= 0 && handleHit(m_loopStart)) return DragHandle::LoopStart;
    if (m_loopEnd >= 0 && handleHit(m_loopEnd)) return DragHandle::LoopEnd;
    return DragHandle::None;
}

void RangeRulerWidget::mousePressEvent(QMouseEvent* event) {
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

void RangeRulerWidget::mouseMoveEvent(QMouseEvent* event) {
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

void RangeRulerWidget::mouseReleaseEvent(QMouseEvent* event) {
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

void RangeRulerWidget::contextMenuEvent(QContextMenuEvent* event) {
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
                   &RangeRulerWidget::clearLoop,
                   &RangeRulerWidget::loopCreated,
                   &RangeRulerWidget::loopRemoved);

    menu.addSeparator();

    addRangeAction(m_rrStart >= 0 && m_rrEnd > m_rrStart,
                   m_rrStart, m_rrEnd,
                   "Set Record Region Here", "Remove Record Region",
                   &RangeRulerWidget::clearRecordRegion,
                   &RangeRulerWidget::recordRegionCreated,
                   &RangeRulerWidget::recordRegionRemoved);

    menu.exec(event->globalPos());
}

void RangeRulerWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), backgroundColor());

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

    paintTicks(painter, startSample, endSample);

    // Playhead marker
    if (m_playheadPos >= 0) {
        int phx = sampleToX(m_playheadPos);
        if (phx >= 0 && phx <= width()) {
            painter.setPen(QPen(QColor("#ff4444"), 2));
            painter.drawLine(phx, 0, phx, height());
            QPolygonF triangle;
            triangle << QPointF(phx - 4, 0) << QPointF(phx + 4, 0) << QPointF(phx, 6);
            painter.setBrush(QColor("#ff4444"));
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(triangle);
        }
    }
}

void RangeRulerWidget::drawRange(QPainter& painter, int64_t rangeStart, int64_t rangeEnd,
                                 const QColor& fill, const QColor& border, const QColor& handleColor,
                                 const QString& label)
{
    int x1 = sampleToX(rangeStart);
    int x2 = sampleToX(rangeEnd);
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
