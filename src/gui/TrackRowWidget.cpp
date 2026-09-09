#include "TrackRowWidget.h"
#include "TrackPanelWidget.h"
#include "TrackColorBar.h"
#include "model/Project.h"
#include <QPainter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QMouseEvent>
#include <QApplication>
#include <cmath>

namespace {
constexpr int kReorderThresholdPx = 8;
}

std::vector<int> scaleTrackHeights(const std::vector<int>& startHeights,
                                   int pressBottom, int targetBottom) {
    if (pressBottom <= 0)
        return startHeights;
    std::vector<int> scaled;
    scaled.reserve(startHeights.size());
    const int floorHeight = vvvdaw::TrackResizeHandleHeight + 1;
    for (int h : startHeights) {
        const long long raw = std::llround(
            static_cast<double>(h) * targetBottom / pressBottom);
        scaled.push_back(static_cast<int>(qBound<long long>(
            static_cast<long long>(floorHeight), raw,
            static_cast<long long>(vvvdaw::MaxTrackHeight))));
    }
    return scaled;
}

TrackResizeHandle::TrackResizeHandle(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(vvvdaw::TrackResizeHandleHeight);
    setCursor(Qt::SizeVerCursor);
    setObjectName("trackResizeHandle");
}

void TrackResizeHandle::setSegmentColors(const QColor& left, const QColor& right,
                                         int splitX) {
    m_left = left;
    m_right = right;
    m_splitX = splitX;
    update();
}

void TrackResizeHandle::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    const QColor left = m_hover ? m_left.lighter(115) : m_left;
    const QColor right = m_hover ? m_right.lighter(115) : m_right;
    p.fillRect(0, 0, m_splitX, height(), left);
    p.fillRect(m_splitX, 0, width() - m_splitX, height(), right);
}

void TrackResizeHandle::enterEvent(QEnterEvent* /*event*/) {
    m_hover = true;
    update();
}

void TrackResizeHandle::leaveEvent(QEvent* /*event*/) {
    m_hover = false;
    update();
}

TrackRowWidget::TrackRowWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_content = new QWidget(this);
    m_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_content, 1);

    m_handle = new TrackResizeHandle(this);
    layout->addWidget(m_handle);

    setMinimumHeight(vvvdaw::TrackResizeHandleHeight);
}

void TrackRowWidget::setHandleColors(const QColor& tint, bool alternateRow) {
    const QColor base = alternateRow ? QColor("#2f2f2f") : QColor("#2a2a2a");
    QColor left = base;
    QColor right = base;
    if (tint.isValid()) {
        // Match the two columns of the row above: the panel's blended color
        // and the timeline's faintly tinted background.
        left = Project::blendColors(base, tint, vvvdaw::TrackRowTintStrength);
        right = Project::blendColors(base, tint, vvvdaw::TrackTimelineTintStrength);
    }
    const int split = (m_colorBar ? m_colorBar->width() : 0)
                    + (m_panel ? m_panel->width() : 0);
    m_handle->setSegmentColors(left, right, split);
}

void TrackRowWidget::assemble(TrackPanelWidget* panel, QSplitter* splitter) {
    m_panel = panel;
    m_splitter = splitter;
    auto* hbox = new QHBoxLayout(m_content);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(0);
    if (m_colorBar)
        hbox->addWidget(m_colorBar);
    hbox->addWidget(panel);
    hbox->addWidget(splitter, 1);
    applyHeight(m_rowHeight);
}

int TrackRowWidget::minimumRowHeight() const {
    int minContent = m_panel ? m_panel->minimumContentHeight() : 0;
    return minContent + vvvdaw::TrackResizeHandleHeight;
}

int TrackRowWidget::clampHeight(int h) const {
    return qBound(minimumRowHeight(), h, vvvdaw::MaxTrackHeight);
}

void TrackRowWidget::applyHeight(int h) {
    m_rowHeight = clampHeight(h);
    setFixedHeight(m_rowHeight);
    int contentHeight = qMax(1, m_rowHeight - vvvdaw::TrackResizeHandleHeight);
    if (m_panel)
        m_panel->applyContentHeight(contentHeight);
}

void TrackRowWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPoint lp = event->position().toPoint();
    const int handleTop = height() - vvvdaw::TrackResizeHandleHeight;
    if (lp.y() >= handleTop) {
        m_resizeDragging = true;
        m_resizeStartGlobalY = event->globalPosition().toPoint().y();
        m_resizeStartHeight = height();
        // The resize mode is captured at press: Shift held while grabbing the
        // handle resizes every track, otherwise only this row.
        m_resizeAll = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
        emit resizeStarted(m_trackIndex, m_resizeStartHeight,
                           event->globalPosition().toPoint());
        return;
    }
    // A press that propagated up from the panel background: begin a reorder
    // drag once the cursor moves past a small threshold.
    m_reorderCandidate = true;
    m_reorderDragging = false;
    m_reorderStartGlobal = event->globalPosition().toPoint();
    event->accept();
}

void TrackRowWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_resizeDragging) {
        const int delta = event->globalPosition().toPoint().y() - m_resizeStartGlobalY;
        const int newH = clampHeight(m_resizeStartHeight + delta);
        emit resizeDragged(m_trackIndex, newH, event->globalPosition().toPoint(),
                           m_resizeAll);
        return;
    }
    if (m_reorderCandidate && (event->buttons() & Qt::LeftButton)) {
        const QPoint gp = event->globalPosition().toPoint();
        if (!m_reorderDragging &&
            (gp - m_reorderStartGlobal).manhattanLength() >= kReorderThresholdPx) {
            m_reorderDragging = true;
            emit reorderDragStarted(m_trackIndex);
        }
        if (m_reorderDragging)
            emit reorderDragMoved(m_trackIndex, gp);
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void TrackRowWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (m_resizeDragging) {
        m_resizeDragging = false;
        emit resizeFinished(m_trackIndex, m_resizeStartHeight, height(), m_resizeAll);
        return;
    }
    if (m_reorderDragging) {
        m_reorderDragging = false;
        emit reorderDragFinished(m_trackIndex, event->globalPosition().toPoint());
    }
    m_reorderCandidate = false;
    m_reorderDragging = false;
    QWidget::mouseReleaseEvent(event);
}
