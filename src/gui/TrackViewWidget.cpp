#include "TrackViewWidget.h"
#include "core/TimeUtils.h"
#include "WaveformPainter.h"
#include "model/Track.h"
#include "model/AudioClip.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <algorithm>
#include <cstdlib>

TrackViewWidget::TrackViewWidget(Track* track, QWidget* parent)
    : QWidget(parent)
    , m_track(track)
{
    setMinimumHeight(60);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
}

void TrackViewWidget::setDragPreview(const AudioEvent* event, int64_t startSample) {
    if (m_dragPreview.event != event || m_dragPreview.startSample != startSample) {
        m_dragPreview = {event, startSample};
        update();
    }
}

void TrackViewWidget::setScrollOffset(int64_t offset) {
    if (offset < 0) offset = 0;
    if (offset != m_scrollOffset) {
        m_scrollOffset = offset;
        update();
        emit scrollOffsetChanged(m_scrollOffset);
    }
}

void TrackViewWidget::setZoom(double pixelsPerSample) {
    double clamped = std::clamp(pixelsPerSample, vvvdaw::MinZoom, vvvdaw::MaxZoom);
    if (clamped != m_pixelsPerSample) {
        m_pixelsPerSample = clamped;
        update();
        emit zoomChanged(m_pixelsPerSample);
    }
}

void TrackViewWidget::updateFromTrack() {
    m_thumbnailCache.clear();
    update();
}

int64_t TrackViewWidget::sampleAtX(int x) const {
    return m_scrollOffset + static_cast<int64_t>(x / m_pixelsPerSample);
}

AudioEvent* TrackViewWidget::eventAtX(int x, int& eventIndex) {
    if (!m_track) return nullptr;
    int64_t s = sampleAtX(x);
    for (size_t i = 0; i < m_track->events().size(); ++i) {
        auto& ev = const_cast<Track*>(m_track)->events()[i];
        int64_t end = ev.startSample() + ev.durationSample();
        if (s >= ev.startSample() && s <= end) {
            eventIndex = static_cast<int>(i);
            return &ev;
        }
    }
    return nullptr;
}

void TrackViewWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.fillRect(rect(), m_alternateRow ? QColor("#2f2f2f") : QColor("#2a2a2a"));

    if (!m_track) return;

    int trackHeight = height();

    // Grid lines
    painter.setPen(QPen(QColor("#3a3a3a"), 1));
    double gridInterval = m_snapUnit;
    if (gridInterval * m_pixelsPerSample < 40) gridInterval *= 4;
    int64_t startSample = m_scrollOffset;
    int64_t endSample = sampleAtX(width());
    double firstGrid = std::floor(startSample / gridInterval) * gridInterval;
    for (double s = firstGrid; s <= endSample; s += gridInterval) {
        int x = static_cast<int>((s - m_scrollOffset) * m_pixelsPerSample);
        painter.drawLine(x, 0, x, trackHeight);
    }

    // Draw events
    for (size_t i = 0; i < m_track->events().size(); ++i) {
        const auto& event = m_track->events()[i];
        if (!event.clip() || !event.clip()->isValid()) continue;

        int x = static_cast<int>((event.startSample() - m_scrollOffset) * m_pixelsPerSample);
        int w = static_cast<int>(event.durationSample() * m_pixelsPerSample);
        if (x + w < 0 || x > width()) continue;

        QRect eventRect(x, 2, w, trackHeight - 4);

        bool isHovered = (static_cast<int>(i) == m_hoverEventIndex);
        bool isDragged = (static_cast<int>(i) == m_dragEventIndex && m_dragging);
        bool isSelected = (static_cast<int>(i) == m_selectedEventIndex);
        if (isDragged && !m_dragSourceVisible) continue;

        // Event background
        QColor bgColor = isSelected ? QColor("#334466")
                       : (isHovered ? QColor("#224466") : QColor("#1a3344"));
        QColor borderColor = isDragged ? QColor("#ffcc00")
                           : (isSelected ? QColor("#ffaa00")
                           : (m_track->isMuted() ? QColor("#666") : QColor("#88ccff")));

        painter.setPen(QPen(borderColor, (isDragged || isSelected) ? 2 : 1));
        painter.setBrush(bgColor);
        painter.drawRect(eventRect);

        // Waveform thumbnail
        {
            int th = eventRect.height() - 2;
            renderThumbnail(painter, event.clip(),
                            eventRect.x() + 1, eventRect.y() + 1, w, th);
        }

        // Border
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(eventRect);

        // Take indicator
        if (event.takes().size() > 1) {
            painter.setPen(QColor("#aaddff"));
            QFont takeFont = painter.font();
            takeFont.setPixelSize(9);
            painter.setFont(takeFont);
            painter.drawText(eventRect.x() + 3, eventRect.y() + 12,
                QString("T%1/%2").arg(event.activeTakeIndex() + 1).arg(event.takes().size()));
        }
    }

    // Drag preview on target track (full rendering, same style as normal event)
    if (m_dragPreview.event && m_dragPreview.event->clip() && m_dragPreview.event->clip()->isValid()) {
        int x = static_cast<int>((m_dragPreview.startSample - m_scrollOffset) * m_pixelsPerSample);
        int w = static_cast<int>(m_dragPreview.event->durationSample() * m_pixelsPerSample);
        if (x + w >= 0 && x <= width()) {
            QRect eventRect(x, 2, w, trackHeight - 4);

            // Full styled rendering like a dragged event
            painter.setPen(QPen(QColor("#ffcc00"), 2));
            painter.setBrush(QColor("#1a3344"));
            painter.drawRect(eventRect);

            // Waveform
            {
                auto clip = m_dragPreview.event->clip();
                int th = eventRect.height() - 2;
                renderThumbnail(painter, clip,
                                eventRect.x() + 1, eventRect.y() + 1, w, th);
            }

            // Border
            painter.setPen(QPen(QColor("#ffcc00"), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(eventRect);
        }
    }

    // Mute overlay
    if (m_track->isMuted()) {
        painter.fillRect(rect(), QColor(0, 0, 0, 80));
    }

    // Playhead line
    if (m_playheadPos >= 0) {
        int phx = static_cast<int>((m_playheadPos - m_scrollOffset) * m_pixelsPerSample);
        if (phx >= 0 && phx <= width()) {
            painter.setPen(QPen(QColor("#ff4444"), 2));
            painter.drawLine(phx, 0, phx, trackHeight);
        }
    }

    // Dragged event tooltip
    if (m_dragging && m_dragEventIndex >= 0) {
        auto& ev = m_track->events()[m_dragEventIndex];
        int phx = static_cast<int>((ev.startSample() - m_scrollOffset) * m_pixelsPerSample);
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setPointSize(9);
        painter.setFont(f);
        painter.drawText(phx + 4, 14,
            QString("Sample: %1").arg(ev.startSample()));
    }
}

void TrackViewWidget::wheelEvent(QWheelEvent* event) {
    int deltaX = static_cast<int>(event->angleDelta().x());
    int deltaY = static_cast<int>(event->angleDelta().y());
    if (event->modifiers() & Qt::ControlModifier && deltaY != 0) {
        double factor = 1.0 + (std::abs(deltaY) / 120.0) * (vvvdaw::ZoomFactor - 1.0);
        if (deltaY > 0)
            setZoom(m_pixelsPerSample * factor);
        else
            setZoom(m_pixelsPerSample / factor);
    } else if (deltaX != 0) {
        setScrollOffset(m_scrollOffset + static_cast<int64_t>(-deltaX * vvvdaw::ScrollStepSamples));
    }
    event->accept();
}

void TrackViewWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_track) {
        int idx = -1;
        AudioEvent* ev = eventAtX(static_cast<int>(event->position().x()), idx);
        if (ev) {
            m_selectedEventIndex = idx;
            m_dragEventIndex = idx;
            m_dragging = true;
            m_dragStartSample = ev->startSample();
            m_dragStartMouseX = static_cast<int>(event->position().x());
            setCursor(Qt::ClosedHandCursor);
            emit eventDragStarted();
        } else {
            m_selectedEventIndex = -1;
        }
        update();
    }
    QWidget::mousePressEvent(event);
}

void TrackViewWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging && m_dragEventIndex >= 0 && m_track) {
        int dx = static_cast<int>(event->position().x()) - m_dragStartMouseX;
        int64_t newStart = m_dragStartSample + static_cast<int64_t>(dx / m_pixelsPerSample);

        if (m_snapToGrid)
            newStart = TimeUtils::snapSample(newStart, m_snapUnit);

        if (newStart < 0) newStart = 0;
        m_track->events()[m_dragEventIndex].setStartSample(newStart);
        emit dragInProgress(m_track->events()[m_dragEventIndex].id(), newStart, event->globalPosition().toPoint());
        update();
    } else {
        // Hover state
        int idx = -1;
        AudioEvent* ev = eventAtX(static_cast<int>(event->position().x()), idx);
        if (idx != m_hoverEventIndex) {
            m_hoverEventIndex = idx;
            setCursor(ev ? Qt::OpenHandCursor : Qt::ArrowCursor);
            update();
        }
    }
}

void TrackViewWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        unsetCursor();
        if (m_track && m_dragEventIndex >= 0) {
            auto& ev = m_track->events()[m_dragEventIndex];
            emit eventMoved(ev.id(), ev.startSample());
            emit eventDragFinished(ev.id(), ev.startSample(), event->globalPosition().toPoint());
        }
        m_dragEventIndex = -1;
        update();
    }
}

void TrackViewWidget::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        m_selectedEventIndex >= 0 && m_track) {
        deleteSelectedEvent();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TrackViewWidget::deleteSelectedEvent() {
    if (!m_track || m_selectedEventIndex < 0 ||
        m_selectedEventIndex >= static_cast<int>(m_track->events().size()))
        return;

    int64_t id = m_track->events()[m_selectedEventIndex].id();
    m_track->removeEvent(id);
    m_selectedEventIndex = -1;
    m_thumbnailCache.clear();
    update();
    emit eventsChanged();
}

void TrackViewWidget::renderThumbnail(QPainter& painter, const std::shared_ptr<AudioClip>& clip,
                                       int x, int y, int w, int h) {
    auto& cache = m_thumbnailCache[clip];
    if (cache.thumbnail.isNull() || cache.thumbnail.width() != w ||
        cache.frameCount != clip->frameCount()) {
        if (!clip->peaks().empty()) {
            cache.thumbnail = WaveformPainter::renderFromPeaks(
                clip->peaks().data(), clip->peaks().size(),
                clip->peaksPerFrame(), clip->frameCount(),
                w, h);
        } else {
            cache.thumbnail = WaveformPainter::render(
                clip->data(), clip->frameCount(), clip->channels(),
                w, h);
        }
        cache.frameCount = clip->frameCount();
    }

    if (!cache.thumbnail.isNull())
        painter.drawImage(x, y, cache.thumbnail);
}

void TrackViewWidget::contextMenuEvent(QContextMenuEvent* event) {
    if (!m_track) {
        QWidget::contextMenuEvent(event);
        return;
    }

    int idx = -1;
    AudioEvent* ev = eventAtX(static_cast<int>(event->pos().x()), idx);
    if (!ev || ev->takes().empty()) {
        QWidget::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    for (size_t i = 0; i < ev->takes().size(); ++i) {
        QString label = QString("Take %1").arg(i + 1);
        if (static_cast<int>(i) == ev->activeTakeIndex())
            label += " ✓";
        QAction* action = menu.addAction(label);
        connect(action, &QAction::triggered, this, [this, ev, i] {
            emit takeSwitchStarted();
            ev->setActiveTake(static_cast<int>(i));
            update();
        });
    }
    menu.exec(event->globalPos());
}


