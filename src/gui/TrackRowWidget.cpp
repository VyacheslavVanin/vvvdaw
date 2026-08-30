#include "TrackRowWidget.h"
#include "TrackPanelWidget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QMouseEvent>
#include <QApplication>

namespace {
constexpr int kReorderThresholdPx = 8;
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

    m_handle = new QWidget(this);
    m_handle->setFixedHeight(vvvdaw::TrackResizeHandleHeight);
    m_handle->setCursor(Qt::SizeVerCursor);
    m_handle->setStyleSheet(
        "background-color: #555; border-top: 1px solid #666;"
        ":hover { background-color: #777; }");
    layout->addWidget(m_handle);

    setMinimumHeight(vvvdaw::TrackResizeHandleHeight);
}

void TrackRowWidget::assemble(TrackPanelWidget* panel, QSplitter* splitter) {
    m_panel = panel;
    m_splitter = splitter;
    auto* hbox = new QHBoxLayout(m_content);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(0);
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
        m_resizeAll = false;
        emit resizeStarted(m_trackIndex, m_resizeStartHeight);
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
        m_resizeAll = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
        emit resizeDragged(m_trackIndex, newH, m_resizeAll);
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
