#include "RangeRulerWidget.h"
#include "core/TimeUtils.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QPoint>
#include <algorithm>
#include <cmath>

RangeRulerWidget::RangeRulerWidget(int fixedHeight, QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(fixedHeight);
    setMouseTracking(true);
    // Creation and removal are handled directly in the mouse handlers (right
    // drag = select+create menu, plain right click = remove menu), so the
    // platform context menu must not race with them.
    setContextMenuPolicy(Qt::NoContextMenu);
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
    const int x = static_cast<int>(event->position().x());
    if (event->button() == Qt::RightButton) {
        beginRightButtonSelect(x);
        return;
    }
    if (event->button() == Qt::LeftButton && m_pixelsPerSample > 0) {
        DragHandle handle = handleAtPos(x);
        if (handle != DragHandle::None) {
            beginHandleDrag(handle, x);
            return;
        }
        emit playheadClicked(sampleAtX(x));
    }
    QWidget::mousePressEvent(event);
}

void RangeRulerWidget::beginRightButtonSelect(int x) {
    m_rightPressed = true;
    m_selecting = false;
    m_selectStartX = x;
    m_selectEndX = x;
}

void RangeRulerWidget::beginHandleDrag(DragHandle handle, int mouseX) {
    m_dragging = true;
    m_dragHandle = handle;
    m_dragStartMouseX = mouseX;
    int64_t* target = nullptr;
    if (handle == DragHandle::LoopStart) target = &m_loopStart;
    else if (handle == DragHandle::LoopEnd) target = &m_loopEnd;
    else if (handle == DragHandle::RRStart) target = &m_rrStart;
    else if (handle == DragHandle::RREnd) target = &m_rrEnd;
    if (target) m_dragStartValue = *target;
}

void RangeRulerWidget::mouseMoveEvent(QMouseEvent* event) {
    const int x = static_cast<int>(event->position().x());
    if (m_rightPressed) {
        handleRightButtonDrag(x);
        return;
    }
    if (m_dragging) {
        int dx = x - m_dragStartMouseX;
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
    DragHandle h = handleAtPos(x);
    setCursor(h != DragHandle::None ? Qt::SplitHCursor : Qt::ArrowCursor);
}

void RangeRulerWidget::handleRightButtonDrag(int x) {
    if (!m_selecting && std::abs(x - m_selectStartX) < 2) return;
    m_selecting = true;
    m_selectEndX = x;
    update();
}

void RangeRulerWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton && m_rightPressed) {
        m_rightPressed = false;
        handleRightButtonRelease(event->globalPosition().toPoint());
        return;
    }
    if (event->button() == Qt::LeftButton && m_dragging) {
        finishHandleDrag();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void RangeRulerWidget::handleRightButtonRelease(const QPoint& globalPos) {
    const bool wasSelecting = m_selecting;
    m_selecting = false;
    update();
    if (wasSelecting) {
        const int x1 = std::min(m_selectStartX, m_selectEndX);
        const int x2 = std::max(m_selectStartX, m_selectEndX);
        const int64_t start = sampleAtX(x1);
        const int64_t end = sampleAtX(x2);
        if (end > start) {
            popupCreateMenu(globalPos, start, end);
            return;
        }
    }
    popupRemoveMenu(globalPos);
}

void RangeRulerWidget::finishHandleDrag() {
    m_dragging = false;
    const DragHandle h = m_dragHandle;
    m_dragHandle = DragHandle::None;
    setCursor(Qt::ArrowCursor);
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
}

void RangeRulerWidget::popupCreateMenu(const QPoint& globalPos, int64_t start, int64_t end) {
    QMenu* menu = buildCreateMenu(start, end);
    menu->exec(globalPos);
    delete menu;
}

QMenu* RangeRulerWidget::buildCreateMenu(int64_t start, int64_t end) {
    QMenu* menu = new QMenu(this);
    QAction* loop = menu->addAction("Create Loop");
    QAction* rr = menu->addAction("Create Record Region");
    QAction* both = menu->addAction("Create Loop and Record Region");
    connect(loop, &QAction::triggered, this, [this, start, end] { createLoopFromRange(start, end); });
    connect(rr, &QAction::triggered, this, [this, start, end] { createRecordRegionFromRange(start, end); });
    connect(both, &QAction::triggered, this, [this, start, end] {
        createLoopFromRange(start, end);
        createRecordRegionFromRange(start, end);
    });
    return menu;
}

void RangeRulerWidget::popupRemoveMenu(const QPoint& globalPos) {
    QMenu* menu = buildRemoveMenu();
    if (!menu->actions().isEmpty())
        menu->exec(globalPos);
    delete menu;
}

QMenu* RangeRulerWidget::buildRemoveMenu() {
    QMenu* menu = new QMenu(this);
    if (m_loopStart >= 0 && m_loopEnd > m_loopStart) {
        QAction* act = menu->addAction("Remove Loop");
        connect(act, &QAction::triggered, this, [this] { clearLoop(); emit loopRemoved(); });
    }
    if (m_rrStart >= 0 && m_rrEnd > m_rrStart) {
        QAction* act = menu->addAction("Remove Record Region");
        connect(act, &QAction::triggered, this, [this] { clearRecordRegion(); emit recordRegionRemoved(); });
    }
    return menu;
}

void RangeRulerWidget::createLoopFromRange(int64_t start, int64_t end) {
    if (end <= start) return;
    m_loopStart = start;
    m_loopEnd = end;
    update();
    emit loopChanged(start, end);
}

void RangeRulerWidget::createRecordRegionFromRange(int64_t start, int64_t end) {
    if (end <= start) return;
    m_rrStart = start;
    m_rrEnd = end;
    update();
    emit recordRegionChanged(start, end);
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

    // Live right-drag selection preview
    if (m_selecting)
        drawSelectionPreview(painter);

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

void RangeRulerWidget::drawSelectionPreview(QPainter& painter) {
    const int x1 = std::clamp(m_selectStartX, 0, width());
    const int x2 = std::clamp(m_selectEndX, 0, width());
    const int selStart = std::min(x1, x2);
    const int selEnd = std::max(x1, x2);
    painter.fillRect(selStart, 0, selEnd - selStart, height(), QColor(90, 130, 255, 70));
    painter.setPen(QPen(QColor(120, 160, 255, 200), 1));
    if (selEnd - selStart > 1) {
        painter.drawLine(selStart, 0, selStart, height());
        painter.drawLine(selEnd, 0, selEnd, height());
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
