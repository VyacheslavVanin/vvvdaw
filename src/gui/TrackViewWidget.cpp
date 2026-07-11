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

TrackViewWidget::EdgeDrag TrackViewWidget::edgeAtX(int x, int eventIndex) const {
    if (!m_track || eventIndex < 0 || eventIndex >= static_cast<int>(m_track->events().size()))
        return EdgeDrag::None;

    const auto& ev = m_track->events()[eventIndex];
    int ex = static_cast<int>((ev.startSample() - m_scrollOffset) * m_pixelsPerSample);
    int ew = static_cast<int>(ev.durationSample() * m_pixelsPerSample);

    if (ew < EdgeHandleWidth * 2) {
        if (x >= ex && x < ex + ew)
            return EdgeDrag::Left;
        return EdgeDrag::None;
    }

    if (x >= ex && x < ex + EdgeHandleWidth)
        return EdgeDrag::Left;
    if (x >= ex + ew - EdgeHandleWidth && x < ex + ew)
        return EdgeDrag::Right;
    return EdgeDrag::None;
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
                            event.offsetSample(), event.durationSample(),
                            eventRect.x() + 1, eventRect.y() + 1, w, th);
        }

        // Edge handles
        {
            QColor handleColor = borderColor.lighter(130);
            painter.setPen(Qt::NoPen);
            painter.setBrush(handleColor);
            int handleH = trackHeight / 3;
            int handleY = (trackHeight - handleH) / 2;
            painter.drawRect(eventRect.x(), handleY, EdgeHandleWidth, handleH);
            painter.drawRect(eventRect.x() + w - EdgeHandleWidth, handleY, EdgeHandleWidth, handleH);
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
                                m_dragPreview.event->offsetSample(), m_dragPreview.event->durationSample(),
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
    int deltaY = static_cast<int>(event->angleDelta().y());
    if (event->modifiers() & Qt::ControlModifier && deltaY != 0) {
        double factor = 1.0 + (std::abs(deltaY) / 120.0) * (vvvdaw::ZoomFactor - 1.0);
        if (deltaY > 0)
            setZoom(m_pixelsPerSample * factor);
        else
            setZoom(m_pixelsPerSample / factor);
    } else if (!m_mouseWheelScroll) {
        int deltaX = static_cast<int>(event->angleDelta().x());
        if (deltaX != 0)
            setScrollOffset(m_scrollOffset + static_cast<int64_t>(-deltaX * vvvdaw::ScrollStepSamples));
    } else {
        if (deltaY != 0)
            setScrollOffset(m_scrollOffset + static_cast<int64_t>(-deltaY * vvvdaw::ScrollStepSamples));
    }
    event->accept();
}

void TrackViewWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_track) {
        int idx = -1;
        AudioEvent* ev = eventAtX(static_cast<int>(event->position().x()), idx);
        if (ev) {
            EdgeDrag edge = edgeAtX(static_cast<int>(event->position().x()), idx);
            if (edge != EdgeDrag::None) {
                m_selectedEventIndex = idx;
                m_edgeDrag = edge;
                m_edgeDragEventIndex = idx;
                m_edgeDragStartOffset = ev->offsetSample();
                m_edgeDragStartDuration = ev->durationSample();
                m_edgeDragStartSample = ev->startSample();
                m_edgeDragStartMouseSample = sampleAtX(static_cast<int>(event->position().x()));
                setCursor(Qt::SizeHorCursor);
                emit eventEdgeTrimStarted();
                update();
                return;
            }

            m_selectedEventIndex = idx;

            bool ctrlDrag = (event->modifiers() & Qt::ControlModifier);
            if (ctrlDrag) {
                emit eventDragStarted();
                AudioEvent copy = *ev;
                copy.setStartSample(copy.startSample() +
                    static_cast<int64_t>(vvvdaw::DefaultSnapUnitSamples));
                m_track->addEvent(std::move(copy));
                m_selectedEventIndex = static_cast<int>(m_track->events().size() - 1);
            }

            m_dragEventIndex = m_selectedEventIndex;
            m_dragging = true;
            m_dragStartSample = m_track->events()[m_selectedEventIndex].startSample();
            m_dragStartMouseX = static_cast<int>(event->position().x());
            setCursor(Qt::ClosedHandCursor);

            if (!ctrlDrag)
                emit eventDragStarted();
        } else {
            m_selectedEventIndex = -1;
        }
        update();
    }
    QWidget::mousePressEvent(event);
}

void TrackViewWidget::mouseMoveEvent(QMouseEvent* event) {
    int mouseX = static_cast<int>(event->position().x());

    if (m_edgeDrag != EdgeDrag::None && m_edgeDragEventIndex >= 0 && m_track) {
        int64_t currentMouseSample = sampleAtX(mouseX);
        int64_t delta = currentMouseSample - m_edgeDragStartMouseSample;

        auto& ev = m_track->events()[m_edgeDragEventIndex];
        auto clip = ev.activeClip();
        int64_t clipFrames = clip ? static_cast<int64_t>(clip->frameCount()) : 0;

        if (m_edgeDrag == EdgeDrag::Left) {
            int64_t maxLeftDelta = m_edgeDragStartOffset;
            int64_t maxRightDelta = m_edgeDragStartDuration - 1;
            if (delta < -maxLeftDelta) delta = -maxLeftDelta;
            if (delta > maxRightDelta) delta = maxRightDelta;

            int64_t newStart = m_edgeDragStartSample + delta;
            int64_t newOffset = m_edgeDragStartOffset + delta;
            int64_t newDuration = m_edgeDragStartDuration - delta;

            if (m_snapToGrid) {
                newStart = TimeUtils::snapSample(newStart, m_snapUnit);
                int64_t actualDelta = newStart - m_edgeDragStartSample;
                newOffset = m_edgeDragStartOffset + actualDelta;
                newDuration = m_edgeDragStartDuration - actualDelta;
            }
            if (newOffset < 0) {
                newDuration += newOffset;
                newStart -= newOffset;
                newOffset = 0;
            }
            if (newDuration < 1) {
                newOffset += newDuration - 1;
                newStart -= newDuration - 1;
                newDuration = 1;
            }
            if (newStart < 0) {
                newOffset += newStart;
                newDuration -= newStart;
                newStart = 0;
            }

            ev.setOffsetSample(newOffset);
            ev.setDurationSample(newDuration);
            ev.setStartSample(newStart);
        } else {
            int64_t maxDelta = clipFrames - m_edgeDragStartOffset - m_edgeDragStartDuration;
            int64_t minDelta = -(m_edgeDragStartDuration - 1);
            if (delta > maxDelta) delta = maxDelta;
            if (delta < minDelta) delta = minDelta;

            int64_t newDuration = m_edgeDragStartDuration + delta;

            if (m_snapToGrid) {
                int64_t endSample = m_edgeDragStartSample + m_edgeDragStartDuration + delta;
                endSample = TimeUtils::snapSample(endSample, m_snapUnit);
                newDuration = endSample - m_edgeDragStartSample;
                if (newDuration < 1) newDuration = 1;
                if (m_edgeDragStartOffset + newDuration > clipFrames)
                    newDuration = clipFrames - m_edgeDragStartOffset;
            }

            ev.setDurationSample(newDuration);
        }
        update();
        return;
    }

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
        AudioEvent* ev = eventAtX(mouseX, idx);
        if (idx != m_hoverEventIndex) {
            m_hoverEventIndex = idx;
        }
        if (ev) {
            EdgeDrag edge = edgeAtX(mouseX, idx);
            if (edge != EdgeDrag::None)
                setCursor(Qt::SizeHorCursor);
            else
                setCursor(Qt::OpenHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        update();
    }
}

void TrackViewWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_edgeDrag != EdgeDrag::None) {
            m_edgeDrag = EdgeDrag::None;
            m_edgeDragEventIndex = -1;
            unsetCursor();
            if (m_track && m_selectedEventIndex >= 0 &&
                m_selectedEventIndex < static_cast<int>(m_track->events().size())) {
                auto& ev = m_track->events()[m_selectedEventIndex];
                emit eventsChanged();
            }
            update();
            return;
        }

        if (m_dragging) {
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
                                       size_t offsetFrame, size_t visibleFrames,
                                       int x, int y, int w, int h) {
    bool isFullClip = (offsetFrame == 0 && visibleFrames == clip->frameCount());

    if (isFullClip) {
        auto& cache = m_thumbnailCache[clip];
        if (cache.thumbnail.isNull() || cache.thumbnail.width() != w ||
            cache.frameCount != clip->frameCount()) {
            if (!clip->peaks().empty()) {
                cache.thumbnail = WaveformPainter::renderFromPeaks(
                    clip->peaks().data(), clip->peaks().size(),
                    clip->peaksPerFrame(), clip->frameCount(),
                    w, h);
            } else {
                cache.thumbnail = QImage();
            }
            cache.frameCount = clip->frameCount();
        }
        if (!cache.thumbnail.isNull())
            painter.drawImage(x, y, cache.thumbnail);
    } else {
        if (!clip->peaks().empty()) {
            QImage thumb = WaveformPainter::renderFromPeaks(
                clip->peaks().data(), clip->peaks().size(),
                clip->peaksPerFrame(), clip->frameCount(),
                offsetFrame, visibleFrames,
                w, h);
            if (!thumb.isNull())
                painter.drawImage(x, y, thumb);
        }
    }
}

void TrackViewWidget::contextMenuEvent(QContextMenuEvent* event) {
    if (!m_track) {
        QWidget::contextMenuEvent(event);
        return;
    }

    int idx = -1;
    AudioEvent* ev = eventAtX(static_cast<int>(event->pos().x()), idx);
    if (!ev) {
        QWidget::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);

    QAction* duplicateAction = menu.addAction("Duplicate");
    connect(duplicateAction, &QAction::triggered, this, [this, eventData = *ev] {
        if (!m_track) return;
        emit takeSwitchStarted();
        AudioEvent copy = eventData;
        copy.setStartSample(copy.startSample() +
            static_cast<int64_t>(vvvdaw::DefaultSnapUnitSamples));
        m_track->addEvent(std::move(copy));
        m_selectedEventIndex = static_cast<int>(m_track->events().size() - 1);
        m_thumbnailCache.clear();
        update();
        emit eventsChanged();
    });

    QAction* deleteAction = menu.addAction("Delete");
    connect(deleteAction, &QAction::triggered, this, [this, idx] {
        if (!m_track) return;
        emit takeSwitchStarted();
        m_selectedEventIndex = idx;
        deleteSelectedEvent();
    });

    if (!ev->takes().empty()) {
        menu.addSeparator();
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
    }

    menu.exec(event->globalPos());
}


