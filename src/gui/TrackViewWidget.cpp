#include "TrackViewWidget.h"
#include "WaveformPainter.h"
#include "model/Track.h"
#include "model/AudioClip.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <algorithm>
#include <cmath>

TrackViewWidget::TrackViewWidget(Track* track, QWidget* parent)
    : QWidget(parent)
    , m_track(track)
{
    setMinimumHeight(60);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
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
    m_pixelsPerSample = std::clamp(pixelsPerSample, 0.000001, 0.1);
    update();
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
        int64_t end = ev.startSample + ev.durationSample;
        if (s >= ev.startSample && s <= end) {
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
    int64_t gridInterval = 48000;
    if (gridInterval * m_pixelsPerSample < 40) gridInterval *= 4;
    int64_t startSample = m_scrollOffset;
    int64_t endSample = sampleAtX(width());
    int64_t firstGrid = (startSample / gridInterval) * gridInterval;
    for (int64_t s = firstGrid; s <= endSample; s += gridInterval) {
        int x = static_cast<int>((s - m_scrollOffset) * m_pixelsPerSample);
        painter.drawLine(x, 0, x, trackHeight);
    }

    // Draw events
    for (size_t i = 0; i < m_track->events().size(); ++i) {
        const auto& event = m_track->events()[i];
        if (!event.clip || !event.clip->isValid()) continue;

        int x = static_cast<int>((event.startSample - m_scrollOffset) * m_pixelsPerSample);
        int w = static_cast<int>(event.durationSample * m_pixelsPerSample);
        if (x + w < 0 || x > width()) continue;

        QRect eventRect(x, 2, w, trackHeight - 4);

        bool isHovered = (static_cast<int>(i) == m_hoverEventIndex);
        bool isDragged = (static_cast<int>(i) == m_dragEventIndex && m_dragging);
        bool isSelected = (static_cast<int>(i) == m_selectedEventIndex);

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
        auto& cache = m_thumbnailCache[event.clip];
        if (cache.thumbnail.isNull() || cache.thumbnail.width() != w ||
            cache.frameCount != event.clip->frameCount()) {
            cache.thumbnail = QImage(w, eventRect.height() - 2, QImage::Format_ARGB32_Premultiplied);
            cache.thumbnail.fill(Qt::transparent);
            if (!event.clip->peaks().empty()) {
                renderWaveform(cache.thumbnail, event.clip->peaks().data(),
                              event.clip->peaks().size(), event.clip->peaksPerFrame(),
                              event.clip->frameCount(), w);
            } else {
                renderWaveform(cache.thumbnail, event.clip->data(),
                              event.clip->frameCount(), event.clip->channels(), w);
            }
            cache.frameCount = event.clip->frameCount();
        }

        if (!cache.thumbnail.isNull()) {
            painter.drawImage(eventRect.x() + 1, eventRect.y() + 1, cache.thumbnail);
        }

        // Border
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(eventRect);
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
        int phx = static_cast<int>((ev.startSample - m_scrollOffset) * m_pixelsPerSample);
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setPointSize(9);
        painter.setFont(f);
        painter.drawText(phx + 4, 14,
            QString("Sample: %1").arg(ev.startSample));
    }
}

void TrackViewWidget::wheelEvent(QWheelEvent* event) {
    int deltaX = static_cast<int>(event->angleDelta().x());
    int deltaY = static_cast<int>(event->angleDelta().y());
    if (event->modifiers() & Qt::ControlModifier) {
        double zoomFactor = (deltaY > 0) ? 1.15 : (1.0 / 1.15);
        setZoom(m_pixelsPerSample * zoomFactor);
    } else if (deltaX != 0) {
        setScrollOffset(m_scrollOffset + static_cast<int64_t>(-deltaX * 48));
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
            m_dragStartSample = ev->startSample;
            m_dragStartMouseX = static_cast<int>(event->position().x());
            setCursor(Qt::SizeHorCursor);
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

        // Snap to grid (48000 sample = 1 sec at 48kHz)
        constexpr int64_t snapInterval = 48000;
        int64_t snapRem = newStart % snapInterval;
        if (snapRem < snapInterval / 2)
            newStart -= snapRem;
        else
            newStart += (snapInterval - snapRem);

        if (newStart < 0) newStart = 0;
        m_track->events()[m_dragEventIndex].startSample = newStart;
        update();
    } else {
        // Hover state
        int idx = -1;
        AudioEvent* ev = eventAtX(static_cast<int>(event->position().x()), idx);
        if (idx != m_hoverEventIndex) {
            m_hoverEventIndex = idx;
            setCursor(ev ? Qt::SizeHorCursor : Qt::ArrowCursor);
            update();
        }
    }
}

void TrackViewWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
        if (m_track && m_dragEventIndex >= 0) {
            auto& ev = m_track->events()[m_dragEventIndex];
            emit eventMoved(ev.id, ev.startSample);
            emit eventDragFinished(ev.id, ev.startSample, event->globalPosition().toPoint());
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

    int64_t id = m_track->events()[m_selectedEventIndex].id;
    m_track->removeEvent(id);
    m_selectedEventIndex = -1;
    m_thumbnailCache.clear();
    update();
    emit eventsChanged();
}

void TrackViewWidget::renderWaveform(QImage& img, const float* samples,
                                      size_t frameCount, int channels, int width) {
    if (!samples || frameCount == 0 || width <= 0) return;

    QPainter painter(&img);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#88ccff"));

    int h2 = img.height() / 2;
    int yCenter = img.rect().y() + h2;

    for (int x = 0; x < width; ++x) {
        size_t startIdx = (frameCount * x) / width;
        size_t endIdx = (frameCount * (x + 1)) / width;
        if (startIdx >= frameCount) break;
        if (endIdx > frameCount) endIdx = frameCount;

        float maxVal = 0.0f;
        for (size_t i = startIdx; i < endIdx; ++i) {
            float s = std::abs(samples[i * channels]);
            if (s > maxVal) maxVal = s;
        }
        maxVal = std::min(maxVal, 1.0f);

        int barHeight = static_cast<int>(maxVal * h2);
        if (barHeight < 1) barHeight = 1;

        painter.drawRect(x, yCenter - barHeight, 1, barHeight * 2);
    }
    painter.end();
}

void TrackViewWidget::renderWaveform(QImage& img, const AudioClip::Peak* peaks,
                                      size_t peakCount, size_t framesPerPeak,
                                      size_t totalFrames, int width) {
    if (!peaks || peakCount == 0 || width <= 0) return;

    QPainter painter(&img);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#88ccff"));

    int h2 = img.height() / 2;
    int yCenter = img.rect().y() + h2;

    for (int x = 0; x < width; ++x) {
        size_t startFrame = (totalFrames * x) / width;
        size_t endFrame = (totalFrames * (x + 1)) / width;
        if (startFrame >= totalFrames) break;
        if (endFrame > totalFrames) endFrame = totalFrames;

        size_t startPeak = startFrame / framesPerPeak;
        size_t endPeak = (endFrame + framesPerPeak - 1) / framesPerPeak;
        if (startPeak >= peakCount) break;
        if (endPeak > peakCount) endPeak = peakCount;

        float maxVal = 0.0f;
        for (size_t i = startPeak; i < endPeak; ++i)
            if (peaks[i].maxAbs > maxVal) maxVal = peaks[i].maxAbs;

        maxVal = std::min(maxVal, 1.0f);
        int barHeight = static_cast<int>(maxVal * h2);
        if (barHeight < 1) barHeight = 1;

        painter.drawRect(x, yCenter - barHeight, 1, barHeight * 2);
    }
    painter.end();
}
