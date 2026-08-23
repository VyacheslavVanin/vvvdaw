#include "TrackViewWidget.h"
#include "core/TimeUtils.h"
#include "WaveformPainter.h"
#include "model/Track.h"
#include "model/Project.h"
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
#include <cmath>

TrackViewWidget::TrackViewWidget(Track* track, Project* project, QWidget* parent)
    : QWidget(parent)
    , m_track(track)
    , m_project(project)
{
    setMinimumHeight(60);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
}

void TrackViewWidget::setDragPreview(const AudioEvent* event, int64_t startSample) {
    if (m_dragPreview.audioEvent != event || m_dragPreview.midiEvent != nullptr
        || m_dragPreview.startSample != startSample) {
        m_dragPreview = {event, nullptr, startSample};
        update();
    }
}

void TrackViewWidget::setMidiDragPreview(const MidiEvent* event, int64_t startSample) {
    if (m_dragPreview.midiEvent != event || m_dragPreview.audioEvent != nullptr
        || m_dragPreview.startSample != startSample) {
        m_dragPreview = {nullptr, event, startSample};
        update();
    }
}

void TrackViewWidget::clearDragPreview() {
    if (m_dragPreview.audioEvent != nullptr || m_dragPreview.midiEvent != nullptr) {
        m_dragPreview = {};
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
    m_midiThumbCache.clear();
    m_decodeCache.clear();
    update();
}

void TrackViewWidget::setSelection(int index) {
    m_selectedEventIds.clear();
    if (index >= 0 && index < eventCount())
        m_selectedEventIds.insert(eventIdAt(index));
    m_selectionAnchorIndex = index;
    update();
    emit selectionChanged();
}

void TrackViewWidget::toggleSelection(int index) {
    if (index < 0 || index >= eventCount()) return;
    int64_t id = eventIdAt(index);
    if (m_selectedEventIds.count(id) > 0)
        m_selectedEventIds.erase(id);
    else
        m_selectedEventIds.insert(id);
    m_selectionAnchorIndex = index;
    update();
    emit selectionChanged();
}

void TrackViewWidget::rangeSelect(int index) {
    if (index < 0 || index >= eventCount()) return;
    int anchor = m_selectionAnchorIndex;
    if (anchor < 0 || anchor >= eventCount())
        anchor = index;
    int lo = std::min(anchor, index);
    int hi = std::max(anchor, index);
    m_selectedEventIds.clear();
    for (int i = lo; i <= hi; ++i)
        m_selectedEventIds.insert(eventIdAt(i));
    m_selectionAnchorIndex = anchor;
    update();
    emit selectionChanged();
}

void TrackViewWidget::clearSelection() {
    m_selectedEventIds.clear();
    m_selectionAnchorIndex = -1;
    update();
    emit selectionChanged();
}

int64_t TrackViewWidget::sampleAtX(int x) const {
    return m_scrollOffset + static_cast<int64_t>(x / m_pixelsPerSample);
}

bool TrackViewWidget::isMidiMode() const {
    return m_track && m_track->type() == Track::Type::Midi;
}

int TrackViewWidget::eventCount() const {
    if (!m_track) return 0;
    return isMidiMode() ? static_cast<int>(m_track->midiEvents().size())
                        : static_cast<int>(m_track->events().size());
}

int64_t TrackViewWidget::eventStart(int index) const {
    if (!m_track || index < 0 || index >= eventCount()) return 0;
    if (isMidiMode()) return m_track->midiEvents()[index].startSample();
    return m_track->events()[index].startSample();
}

int64_t TrackViewWidget::eventOffset(int index) const {
    if (!m_track || index < 0 || index >= eventCount()) return 0;
    if (isMidiMode()) return m_track->midiEvents()[index].offsetSample();
    return m_track->events()[index].offsetSample();
}

int64_t TrackViewWidget::eventDuration(int index) const {
    if (!m_track || index < 0 || index >= eventCount()) return 0;
    if (isMidiMode()) return m_track->midiEvents()[index].durationSample();
    return m_track->events()[index].durationSample();
}

int64_t TrackViewWidget::eventIdAt(int index) const {
    if (!m_track || index < 0 || index >= eventCount()) return -1;
    if (isMidiMode()) return m_track->midiEvents()[index].id();
    return m_track->events()[index].id();
}

bool TrackViewWidget::eventHasTakes(int index) const {
    if (!m_track || index < 0 || index >= eventCount()) return false;
    if (isMidiMode()) return !m_track->midiEvents()[index].takes().empty();
    return !m_track->events()[index].takes().empty();
}

int TrackViewWidget::eventActiveTake(int index) const {
    if (!m_track || index < 0 || index >= eventCount()) return -1;
    if (isMidiMode()) return m_track->midiEvents()[index].activeTakeIndex();
    return m_track->events()[index].activeTakeIndex();
}

int TrackViewWidget::eventTakeCount(int index) const {
    if (!m_track || index < 0 || index >= eventCount()) return 0;
    if (isMidiMode()) return static_cast<int>(m_track->midiEvents()[index].takes().size());
    return static_cast<int>(m_track->events()[index].takes().size());
}

void TrackViewWidget::setEventStart(int index, int64_t value) {
    if (!m_track || index < 0 || index >= eventCount()) return;
    if (isMidiMode()) m_track->midiEvents()[index].setStartSample(value);
    else m_track->events()[index].setStartSample(value);
}

void TrackViewWidget::setEventOffset(int index, int64_t value) {
    if (!m_track || index < 0 || index >= eventCount()) return;
    if (isMidiMode()) m_track->midiEvents()[index].setOffsetSample(value);
    else m_track->events()[index].setOffsetSample(value);
}

void TrackViewWidget::setEventDuration(int index, int64_t value) {
    if (!m_track || index < 0 || index >= eventCount()) return;
    if (isMidiMode()) m_track->midiEvents()[index].setDurationSample(value);
    else m_track->events()[index].setDurationSample(value);
}

void TrackViewWidget::switchEventTake(int index, int takeIndex) {
    if (!m_track || index < 0 || index >= eventCount()) return;
    if (isMidiMode()) m_track->midiEvents()[index].setActiveTake(takeIndex);
    else m_track->events()[index].setActiveTake(takeIndex);
}

std::shared_ptr<MidiClip> TrackViewWidget::midiClipAt(int index) const {
    if (!m_track || index < 0 || index >= eventCount()) return nullptr;
    if (isMidiMode()) return m_track->midiEvents()[index].activeClip();
    return nullptr;
}

int TrackViewWidget::eventAtX(int x, int& eventIndex) {
    if (!m_track) return -1;
    int64_t s = sampleAtX(x);
    int n = eventCount();
    for (int i = 0; i < n; ++i) {
        int64_t end = eventStart(i) + eventDuration(i);
        if (s >= eventStart(i) && s <= end) {
            eventIndex = i;
            return i;
        }
    }
    return -1;
}

TrackViewWidget::EdgeDrag TrackViewWidget::edgeAtX(int x, int eventIndex) const {
    if (!m_track || eventIndex < 0 || eventIndex >= eventCount())
        return EdgeDrag::None;

    int64_t ex = static_cast<int64_t>((eventStart(eventIndex) - m_scrollOffset) * m_pixelsPerSample);
    int64_t ew = static_cast<int64_t>(eventDuration(eventIndex) * m_pixelsPerSample);

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

void TrackViewWidget::setRecordingPreview(bool active, int64_t startSample,
                                          int64_t endSample) {
    if (m_recordingActive == active && m_recordStartSample == startSample
        && m_recordEndSample == endSample)
        return;
    m_recordingActive = active;
    m_recordStartSample = startSample;
    m_recordEndSample = endSample;
    if (!active) {
        m_recordingPeaks.clear();
        m_recordingWaveCache = RecordingWaveCache();
    }
    update();
}

void TrackViewWidget::setRecordingPeaks(std::vector<AudioClip::Peak> peaks,
                                        int64_t framesPerPeak,
                                        int64_t recordedFrames) {
    m_recordingPeaks = std::move(peaks);
    m_recordingFramesPerPeak = framesPerPeak;
    m_recordingRecordedFrames = recordedFrames;
    m_recordingWaveCache = RecordingWaveCache();
    update();
}

void TrackViewWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.fillRect(rect(), m_alternateRow ? QColor("#2f2f2f") : QColor("#2a2a2a"));

    if (!m_track) return;

    int trackHeight = height();
    drawGrid(painter, trackHeight);
    drawEvents(painter, trackHeight);

    if (!isMidiMode())
        drawCrossfades(painter);

    drawDragPreview(painter, trackHeight);
    drawRecordingPreview(painter, trackHeight);
    drawMuteOverlay(painter);
    drawPlayhead(painter, trackHeight);
    drawMouseCursor(painter, trackHeight);
    drawDragTooltip(painter);
}

void TrackViewWidget::drawGrid(QPainter& painter, int trackHeight) {
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
}

void TrackViewWidget::drawEvents(QPainter& painter, int trackHeight) {
    int n = eventCount();
    for (int i = 0; i < n; ++i)
        drawEventRow(painter, i, trackHeight);
}

void TrackViewWidget::drawEventRow(QPainter& painter, int index, int trackHeight) {
    if (!eventHasTakes(index) && !isMidiMode()) {
        auto& ev = m_track->events()[index];
        if (!ev.clip() || !ev.clip()->isValid()) return;
    }

    // Pixel math in int64: at deep zoom a long clip can exceed int range.
    int64_t x64 = static_cast<int64_t>(
        (eventStart(index) - m_scrollOffset) * m_pixelsPerSample);
    int64_t w64 = static_cast<int64_t>(eventDuration(index) * m_pixelsPerSample);
    if (x64 + w64 < 0 || x64 > width()) return;
    int x = static_cast<int>(std::clamp<int64_t>(x64, -2000000000LL, 2000000000LL));
    // The waveform itself is clipped to the visible window; the event rect
    // only needs to cover the viewport.
    int w = static_cast<int>(std::min<int64_t>(w64, 2LL * width() + 2000));

    QRect eventRect(x, 2, w, trackHeight - 4);

    bool isHovered = (index == m_hoverEventIndex);
    bool isDragged = (index == m_dragEventIndex && m_dragging);
    bool isSelected = eventIsSelected(index);
    if (isDragged && !m_dragSourceVisible) return;

    QColor bgColor = isSelected ? QColor("#334466")
                   : (isHovered ? QColor("#224466") : QColor("#1a3344"));
    QColor borderColor = isDragged ? QColor("#ffcc00")
                       : (isSelected ? QColor("#ffaa00")
                       : (m_track->isMuted() ? QColor("#666") : QColor("#88ccff")));

    painter.setPen(QPen(borderColor, (isDragged || isSelected) ? 2 : 1));
    painter.setBrush(bgColor);
    painter.drawRect(eventRect);

    if (isMidiMode()) {
        auto clip = midiClipAt(index);
        if (clip) {
            int th = eventRect.height() - 2;
            renderMidiPreview(painter, clip, eventOffset(index), eventDuration(index),
                              eventRect.x() + 1, eventRect.y() + 1, w, th);
        }
    } else {
        int th = eventRect.height() - 2;
        auto& ev = m_track->events()[index];
        renderThumbnail(painter, ev.clip(),
                        ev.startSample(), ev.durationSample(),
                        static_cast<size_t>(ev.offsetSample()),
                        static_cast<size_t>(ev.sourceFrames()),
                        eventRect.y() + 1, th);
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
    if (eventTakeCount(index) > 1) {
        painter.setPen(QColor("#aaddff"));
        QFont takeFont = painter.font();
        takeFont.setPixelSize(9);
        painter.setFont(takeFont);
        painter.drawText(eventRect.x() + 3, eventRect.y() + 12,
            QString("T%1/%2").arg(eventActiveTake(index) + 1).arg(eventTakeCount(index)));
    }
}

void TrackViewWidget::drawDragPreview(QPainter& painter, int trackHeight) {
    if (m_dragPreview.midiEvent && m_dragPreview.midiEvent->clip()) {
        int x = static_cast<int>((m_dragPreview.startSample - m_scrollOffset) * m_pixelsPerSample);
        int w = static_cast<int>(m_dragPreview.midiEvent->durationSample() * m_pixelsPerSample);
        if (x + w >= 0 && x <= width()) {
            QRect eventRect(x, 2, w, trackHeight - 4);

            painter.setPen(QPen(QColor("#ffcc00"), 2));
            painter.setBrush(QColor("#1a3344"));
            painter.drawRect(eventRect);

            {
                auto clip = m_dragPreview.midiEvent->clip();
                int th = eventRect.height() - 2;
                renderMidiPreview(painter, clip,
                                  m_dragPreview.midiEvent->offsetSample(),
                                  m_dragPreview.midiEvent->durationSample(),
                                  eventRect.x() + 1, eventRect.y() + 1, w, th);
            }

            painter.setPen(QPen(QColor("#ffcc00"), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(eventRect);
        }
    } else if (m_dragPreview.audioEvent && m_dragPreview.audioEvent->clip()
               && m_dragPreview.audioEvent->clip()->isValid()) {
        int64_t x64 = static_cast<int64_t>(
            (m_dragPreview.startSample - m_scrollOffset) * m_pixelsPerSample);
        int64_t w64 = static_cast<int64_t>(
            m_dragPreview.audioEvent->durationSample() * m_pixelsPerSample);
        if (x64 + w64 >= 0 && x64 <= width()) {
            int x = static_cast<int>(std::clamp<int64_t>(x64, -2000000000LL, 2000000000LL));
            int w = static_cast<int>(std::min<int64_t>(w64, 2LL * width() + 2000));
            QRect eventRect(x, 2, w, trackHeight - 4);

            painter.setPen(QPen(QColor("#ffcc00"), 2));
            painter.setBrush(QColor("#1a3344"));
            painter.drawRect(eventRect);

            {
                auto clip = m_dragPreview.audioEvent->clip();
                int th = eventRect.height() - 2;
                renderThumbnail(painter, clip,
                                m_dragPreview.startSample,
                                m_dragPreview.audioEvent->durationSample(),
                                static_cast<size_t>(m_dragPreview.audioEvent->offsetSample()),
                                static_cast<size_t>(m_dragPreview.audioEvent->sourceFrames()),
                                eventRect.y() + 1, th);
            }

            painter.setPen(QPen(QColor("#ffcc00"), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(eventRect);
        }
    }
}

void TrackViewWidget::drawRecordingPreview(QPainter& painter, int trackHeight) {
    // Live audio recording preview: a rectangle growing with the playhead so
    // it is visible that recording is in progress before the event is written.
    if (!(m_recordingActive && m_track && m_track->isRecordArmed()
          && m_track->type() == Track::Type::Audio))
        return;

    int64_t end = m_playheadPos;
    if (m_recordEndSample >= 0 && end > m_recordEndSample)
        end = m_recordEndSample;
    if (end <= m_recordStartSample)
        return;

    int rx = static_cast<int>((m_recordStartSample - m_scrollOffset) * m_pixelsPerSample);
    int rw = static_cast<int>((end - m_recordStartSample) * m_pixelsPerSample);
    if (rx + rw < 0 || rx > width())
        return;

    QRect recRect(rx, 2, rw, trackHeight - 4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 180, 100, 55));
    painter.drawRect(recRect);

    renderRecordingWaveform(painter, trackHeight);

    painter.setPen(QPen(QColor("#00d06a"), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(recRect);
}

void TrackViewWidget::renderRecordingWaveform(QPainter& painter, int trackHeight) {
    // Live waveform of the audio captured so far, drawn at true sample
    // positions: each refresh appends a new chunk to the right and the
    // not-yet-recorded part of the rectangle stays empty. Only the
    // viewport-visible window is rendered, re-rendered when new peaks arrive
    // or the view scrolls/zooms.
    if (m_recordingPeaks.empty() || m_recordingFramesPerPeak <= 0
        || m_recordingRecordedFrames <= 0)
        return;

    const int64_t recStart = m_recordStartSample;
    const int64_t recFrames = m_recordingRecordedFrames;
    const int64_t viewLeft = m_scrollOffset;
    const int64_t viewRight = sampleAtX(width());
    const int64_t wl = std::max<int64_t>(0, viewLeft - recStart);
    const int64_t wr = std::min<int64_t>(recFrames, viewRight - recStart);
    if (wr <= wl)
        return;

    const int ih = std::max(1, trackHeight - 6);
    const int iw = std::max(1, static_cast<int>(
        std::llround(static_cast<double>(wr - wl) * m_pixelsPerSample)));
    const int imgX = static_cast<int>(std::llround(
        static_cast<double>(recStart + wl - m_scrollOffset) * m_pixelsPerSample));
    const double dpr = devicePixelRatio();
    auto& cache = m_recordingWaveCache;
    if (cache.image.isNull()
        || cache.peakCount != static_cast<int64_t>(m_recordingPeaks.size())
        || cache.offsetFrame != wl
        || cache.visibleFrames != (wr - wl)
        || cache.width != iw || cache.height != ih
        || cache.devicePixelRatio != dpr) {
        cache.image = WaveformPainter::renderFromPeaks(
            m_recordingPeaks.data(), m_recordingPeaks.size(),
            static_cast<size_t>(m_recordingFramesPerPeak),
            static_cast<size_t>(recFrames),
            static_cast<size_t>(wl), static_cast<size_t>(wr - wl),
            iw, ih, dpr, WaveformPainter::recordingColor());
        cache.peakCount = static_cast<int64_t>(m_recordingPeaks.size());
        cache.offsetFrame = wl;
        cache.visibleFrames = wr - wl;
        cache.width = iw;
        cache.height = ih;
        cache.devicePixelRatio = dpr;
    }
    if (!cache.image.isNull())
        painter.drawImage(imgX, 3, cache.image);
}

void TrackViewWidget::drawMuteOverlay(QPainter& painter) {
    if (!m_track->isMuted())
        return;
    painter.fillRect(rect(), QColor(0, 0, 0, 80));
}

void TrackViewWidget::drawPlayhead(QPainter& painter, int trackHeight) {
    if (m_playheadPos < 0)
        return;
    int phx = static_cast<int>((m_playheadPos - m_scrollOffset) * m_pixelsPerSample);
    if (phx < 0 || phx > width())
        return;
    painter.setPen(QPen(QColor("#ff4444"), 2));
    painter.drawLine(phx, 0, phx, trackHeight);
}

void TrackViewWidget::drawMouseCursor(QPainter& painter, int trackHeight) {
    // Thin vertical cursor line at the mouse position
    if (m_mouseX < 0 || m_mouseX > width())
        return;
    painter.setPen(QPen(QColor(255, 255, 255, 110), 1));
    painter.drawLine(m_mouseX, 0, m_mouseX, trackHeight);
}

void TrackViewWidget::drawDragTooltip(QPainter& painter) {
    // Dragged event tooltip
    if (!m_dragging || m_dragEventIndex < 0)
        return;
    int64_t start = eventStart(m_dragEventIndex);
    int phx = static_cast<int>((start - m_scrollOffset) * m_pixelsPerSample);
    painter.setPen(Qt::white);
    QFont f = painter.font();
    f.setPointSize(9);
    painter.setFont(f);
    painter.drawText(phx + 4, 14, QString("Sample: %1").arg(start));
}

void TrackViewWidget::renderMidiPreview(QPainter& painter, const std::shared_ptr<MidiClip>& clip,
                                        int64_t offsetSample, int64_t durationSample,
                                        int x, int y, int w, int h) {
    if (!clip || clip->notes().empty() || durationSample <= 0) return;
    if (m_samplesPerTick <= 0) return;

    // Visible tick window of the clip mapped onto the event's width.
    double spt = m_samplesPerTick;
    double startTick = static_cast<double>(offsetSample) / spt;
    double endTick = static_cast<double>(offsetSample + durationSample) / spt;
    double spanTicks = endTick - startTick;
    if (spanTicks <= 0) return;

    auto& cache = m_midiThumbCache[clip];
    if (cache.image.isNull() || cache.revision != clip->revision()
        || cache.offsetSample != offsetSample || cache.durationSample != durationSample
        || cache.samplesPerTick != spt
        || cache.image.width() != w || cache.image.height() != h) {
        cache.image = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        cache.image.fill(Qt::transparent);
        cache.revision = clip->revision();
        cache.offsetSample = offsetSample;
        cache.durationSample = durationSample;
        cache.samplesPerTick = spt;

        QPainter p(&cache.image);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#7fb4e0"));

        constexpr double kMinPitch = 36.0;
        constexpr double kMaxPitch = 84.0;
        const double pitchRange = kMaxPitch - kMinPitch;
        for (const auto& note : clip->notes()) {
            double noteStart = static_cast<double>(note.startTick);
            double noteEnd = static_cast<double>(note.endTick());
            if (noteEnd <= startTick || noteStart >= endTick)
                continue; // outside the visible window

            double visStart = std::max(noteStart, startTick);
            double visEnd = std::min(noteEnd, endTick);

            double pitchFrac = (note.pitch - kMinPitch) / pitchRange;
            pitchFrac = std::clamp(pitchFrac, 0.0, 1.0);
            // Coordinates are relative to the cache image (drawn at (x,y) below).
            int ny = static_cast<int>((1.0 - pitchFrac) * (h - 2));
            int nx = static_cast<int>((visStart - startTick) / spanTicks * w);
            int nw = std::max(1, static_cast<int>((visEnd - visStart) / spanTicks * w));
            p.drawRect(nx, ny, nw, std::max(2, h / 16));
        }
    }

    if (!cache.image.isNull())
        painter.drawImage(x, y, cache.image);
}

void TrackViewWidget::drawCrossfades(QPainter& painter) {
    if (!m_track || m_track->type() != Track::Type::Audio)
        return;
    const auto& events = m_track->events();
    if (events.size() < 2)
        return;

    // The event vector is insertion-ordered; pair events by timeline position
    // so adjacent events are compared regardless of move history.
    std::vector<int> order(events.size());
    for (size_t i = 0; i < events.size(); ++i)
        order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&events](int a, int b) {
        return events[a].startSample() < events[b].startSample();
    });

    const int trackHeight = height();
    const QColor crossfadeColor("#ff6600");
    painter.setPen(QPen(crossfadeColor, 1));
    painter.setBrush(Qt::NoBrush);

    for (size_t k = 0; k + 1 < order.size(); ++k) {
        const AudioEvent& left = events[order[k]];
        const AudioEvent& right = events[order[k + 1]];
        if (left.fadeOutSamples() <= 0 || right.fadeInSamples() <= 0)
            continue;

        // Junction zone: from where the left event starts fading out to where
        // the right event finishes fading in. Drawn even across a gap.
        const int64_t fadeStart = left.endSample() - left.fadeOutSamples();
        const int64_t fadeEnd = right.startSample() + right.fadeInSamples();
        if (fadeEnd <= fadeStart)
            continue;

        const int64_t x1 = static_cast<int64_t>(
            (fadeStart - m_scrollOffset) * m_pixelsPerSample);
        const int64_t x2 = static_cast<int64_t>(
            (fadeEnd - m_scrollOffset) * m_pixelsPerSample);
        if (x2 < 0 || x1 > width())
            continue;

        const int px1 = static_cast<int>(std::clamp<int64_t>(x1, 0, width()));
        const int px2 = static_cast<int>(std::clamp<int64_t>(x2, 0, width()));
        if (px2 <= px1)
            continue;

        const int y = 2;
        const int h = trackHeight - 4;
        painter.drawRect(px1, y, px2 - px1, h);
        painter.drawLine(px1, y, px2, y + h);
        painter.drawLine(px2, y, px1, y + h);
    }
}

void TrackViewWidget::wheelEvent(QWheelEvent* event) {
    int deltaY = static_cast<int>(event->angleDelta().y());
    if (!(event->modifiers() & Qt::ControlModifier) || deltaY == 0) {
        // Plain wheel: let the enclosing scroll area handle it (scrolls the
        // track list vertically). Horizontal panning is done by middle-drag.
        event->ignore();
        return;
    }

    // Zoom anchored at the frame under the cursor: the sample under the
    // mouse must stay under it after the zoom.
    int mouseX = std::clamp(static_cast<int>(event->position().x()), 0, std::max(0, width() - 1));
    const int64_t anchorSample = sampleAtX(mouseX);
    double factor = 1.0 + (std::abs(deltaY) / 120.0) * (vvvdaw::ZoomFactor - 1.0);
    if (deltaY > 0)
        setZoom(m_pixelsPerSample * factor);
    else
        setZoom(m_pixelsPerSample / factor);
    setScrollOffset(anchorSample - static_cast<int64_t>(mouseX / m_pixelsPerSample));
    event->accept();
}

void TrackViewWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStartX = static_cast<int>(event->position().x());
        m_panStartOffset = m_scrollOffset;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_track) {
        int idx = -1;
        if (eventAtX(static_cast<int>(event->position().x()), idx) >= 0) {
            EdgeDrag edge = edgeAtX(static_cast<int>(event->position().x()), idx);
            if (edge != EdgeDrag::None) {
                setSelection(idx);
                m_edgeDrag = edge;
                m_edgeDragEventIndex = idx;
                m_edgeDragStartOffset = eventOffset(idx);
                m_edgeDragStartDuration = eventDuration(idx);
                m_edgeDragStartSample = eventStart(idx);
                m_edgeDragStartMouseSample = sampleAtX(static_cast<int>(event->position().x()));
                setCursor(Qt::SizeHorCursor);
                update();
                return;
            }

            bool ctrlDrag = (event->modifiers() & Qt::ControlModifier);
            bool shiftDrag = (event->modifiers() & Qt::ShiftModifier);

            if (isMidiMode()) {
                // Legacy MIDI behavior: Ctrl/Shift-drag duplicates.
                setSelection(idx);
                bool duplicate = ctrlDrag || shiftDrag;
                m_dragWasDuplicate = duplicate;
                if (duplicate) {
                    emit eventDragStarted();
                    auto lock = m_project ? m_project->writeLock()
                                          : std::unique_lock<std::shared_mutex>();
                    MidiEvent copy = shiftDrag ? m_track->midiEvents()[idx].cloneDeep()
                                               : m_track->midiEvents()[idx];
                    copy.setStartSample(copy.startSample() +
                        static_cast<int64_t>(vvvdaw::DefaultSnapUnitSamples));
                    m_track->addMidiEvent(std::move(copy));
                    setSelection(eventCount() - 1);
                }
                m_dragEventIndex = idx;
                if (duplicate)
                    m_dragEventIndex = eventCount() - 1;
                m_dragging = true;
                m_dragStartSample = eventStart(m_dragEventIndex);
                m_dragStartMouseX = static_cast<int>(event->position().x());
                setCursor(Qt::ClosedHandCursor);
                update();
                return;
            }

            // Audio tracks: Ctrl+click toggles selection (or starts a deferred
            // duplicate drag once the mouse moves), Shift+click range-selects.
            if (ctrlDrag) {
                m_dragWasDuplicate = true;
                m_pendingDuplicateDrag = true;
                m_dragEventIndex = idx;
                m_dragging = true;
                m_dragStartSample = eventStart(idx);
                m_dragStartMouseX = static_cast<int>(event->position().x());
                setCursor(Qt::ClosedHandCursor);
                update();
                return;
            }
            if (shiftDrag) {
                rangeSelect(idx);
                update();
                return;
            }

            setSelection(idx);
            m_dragEventIndex = idx;
            m_dragging = true;
            m_dragStartSample = eventStart(idx);
            m_dragStartMouseX = static_cast<int>(event->position().x());
            setCursor(Qt::ClosedHandCursor);
        } else {
            clearSelection();
        }
        update();
    }
    QWidget::mousePressEvent(event);
}

void TrackViewWidget::mouseMoveEvent(QMouseEvent* event) {
    int mouseX = static_cast<int>(event->position().x());

    if (mouseX != m_mouseX) {
        m_mouseX = mouseX;
        update();
    }

    if (m_panning) {
        int dx = mouseX - m_panStartX;
        setScrollOffset(m_panStartOffset - static_cast<int64_t>(dx / m_pixelsPerSample));
        return;
    }

    if (m_edgeDrag != EdgeDrag::None && m_edgeDragEventIndex >= 0 && m_track) {
        int64_t currentMouseSample = sampleAtX(mouseX);
        int64_t delta = currentMouseSample - m_edgeDragStartMouseSample;

        int idx = m_edgeDragEventIndex;
        int64_t clipFrames = -1;
        if (!isMidiMode()) {
            auto clip = m_track->events()[idx].activeClip();
            if (clip) clipFrames = static_cast<int64_t>(clip->frameCount());
        }

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

            setEventOffset(idx, newOffset);
            setEventDuration(idx, newDuration);
            setEventStart(idx, newStart);
        } else {
            int64_t maxDelta = (clipFrames > 0)
                ? clipFrames - m_edgeDragStartOffset - m_edgeDragStartDuration
                : INT64_MAX;
            int64_t minDelta = -(m_edgeDragStartDuration - 1);
            if (delta > maxDelta) delta = maxDelta;
            if (delta < minDelta) delta = minDelta;

            int64_t newDuration = m_edgeDragStartDuration + delta;

            if (m_snapToGrid) {
                int64_t endSample = m_edgeDragStartSample + m_edgeDragStartDuration + delta;
                endSample = TimeUtils::snapSample(endSample, m_snapUnit);
                newDuration = endSample - m_edgeDragStartSample;
                if (newDuration < 1) newDuration = 1;
                if (clipFrames > 0 && m_edgeDragStartOffset + newDuration > clipFrames)
                    newDuration = clipFrames - m_edgeDragStartOffset;
            }

            setEventDuration(idx, newDuration);
        }
        update();
        return;
    }

    if (m_dragging && m_dragEventIndex >= 0 && m_track) {
        if (m_pendingDuplicateDrag) {
            // Still within the click threshold: treat as a potential Ctrl-click
            // toggle, not a drag yet.
            if (std::abs(mouseX - m_dragStartMouseX) <= kDuplicateDragThresholdPx) {
                update();
                return;
            }
            // Past the threshold: materialize the duplicate and drag the copy.
            emit eventDragStarted();
            auto lock = m_project ? m_project->writeLock()
                                  : std::unique_lock<std::shared_mutex>();
            AudioEvent copy = m_track->events()[m_dragEventIndex];
            copy.setStartSample(copy.startSample() +
                static_cast<int64_t>(vvvdaw::DefaultSnapUnitSamples));
            m_track->addEvent(std::move(copy));
            m_dragEventIndex = eventCount() - 1;
            setSelection(m_dragEventIndex);
            m_pendingDuplicateDrag = false;
            m_dragStartSample = eventStart(m_dragEventIndex);
            m_dragStartMouseX = mouseX;
            m_thumbnailCache.clear();
            m_decodeCache.clear();
        }

        int dx = static_cast<int>(event->position().x()) - m_dragStartMouseX;
        int64_t newStart = m_dragStartSample + static_cast<int64_t>(dx / m_pixelsPerSample);

        if (m_snapToGrid)
            newStart = TimeUtils::snapSample(newStart, m_snapUnit);

        if (newStart < 0) newStart = 0;
        setEventStart(m_dragEventIndex, newStart);
        emit dragInProgress(eventIdAt(m_dragEventIndex), newStart, event->globalPosition().toPoint());
        update();
    } else {
        // Hover state
        int idx = -1;
        bool hit = (eventAtX(mouseX, idx) >= 0);
        if (idx != m_hoverEventIndex) {
            m_hoverEventIndex = idx;
        }
        if (hit) {
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
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (m_edgeDrag != EdgeDrag::None) {
            int idx = m_edgeDragEventIndex;
            m_edgeDrag = EdgeDrag::None;
            m_edgeDragEventIndex = -1;
            unsetCursor();
            if (m_track && idx >= 0 && idx < eventCount()) {
                int64_t id = eventIdAt(idx);
                emit eventTrimFinished(id,
                    m_edgeDragStartSample, eventStart(idx),
                    m_edgeDragStartOffset, eventOffset(idx),
                    m_edgeDragStartDuration, eventDuration(idx));
            }
            update();
            return;
        }

        if (m_dragging) {
            m_dragging = false;
            unsetCursor();
            if (m_pendingDuplicateDrag) {
                // Ctrl-click without movement: toggle the event's selection
                // instead of duplicating it.
                m_pendingDuplicateDrag = false;
                int idx = m_dragEventIndex;
                m_dragEventIndex = -1;
                m_dragWasDuplicate = false;
                toggleSelection(idx);
                update();
                return;
            }
            if (m_track && m_dragEventIndex >= 0) {
                int64_t id = eventIdAt(m_dragEventIndex);
                int64_t start = eventStart(m_dragEventIndex);
                emit eventMoved(id, start);
                emit eventDragFinished(id, m_dragStartSample, start,
                                       event->globalPosition().toPoint(),
                                       m_dragWasDuplicate);
            }
            m_dragEventIndex = -1;
            m_dragWasDuplicate = false;
            update();
        }
    }
}

void TrackViewWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isMidiMode()) {
        int idx = -1;
        if (eventAtX(static_cast<int>(event->position().x()), idx) >= 0) {
            setSelection(idx);
            emit eventDoubleClicked(eventIdAt(idx));
            update();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TrackViewWidget::leaveEvent(QEvent* event) {
    if (m_mouseX != -1) {
        m_mouseX = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

void TrackViewWidget::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        hasSelection() && m_track) {
        deleteSelectedEvent();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TrackViewWidget::deleteSelectedEvent() {
    if (!m_track || m_selectedEventIds.empty())
        return;

    auto lock = m_project ? m_project->writeLock()
                          : std::unique_lock<std::shared_mutex>();
    std::vector<int64_t> ids(m_selectedEventIds.begin(), m_selectedEventIds.end());
    for (int64_t id : ids) {
        if (isMidiMode())
            m_track->removeMidiEvent(id);
        else
            m_track->removeEvent(id);
    }
    clearSelection();
    m_thumbnailCache.clear();
    m_midiThumbCache.clear();
    m_decodeCache.clear();
    update();
    emit eventsChanged();
}

void TrackViewWidget::renderThumbnail(QPainter& painter, const std::shared_ptr<AudioClip>& clip,
                                       int64_t eventStartSample, int64_t eventDuration,
                                       size_t offsetFrame, size_t sourceFrames,
                                       int y, int h) {
    if (!clip || !clip->isValid() || h <= 0)
        return;

    // Only the viewport-visible part of the event is rasterized: at deep zoom
    // rendering the whole event would create enormous images for no benefit.
    const double pps = m_pixelsPerSample;
    const int64_t evFrom = eventStartSample;
    const int64_t evTo = eventStartSample + eventDuration;
    const int64_t viewFrom = m_scrollOffset;
    const int64_t viewTo = m_scrollOffset
        + static_cast<int64_t>(std::ceil(static_cast<double>(width()) / pps));

    int64_t tlFrom = std::max(evFrom, viewFrom);
    int64_t tlTo = std::min(evTo, viewTo);
    if (tlTo <= tlFrom) return;

    size_t clipFrom = static_cast<size_t>(tlFrom - evFrom) + offsetFrame;
    size_t clipTo = static_cast<size_t>(tlTo - evFrom) + offsetFrame;
    const size_t srcEnd = offsetFrame + sourceFrames;
    if (clipFrom < offsetFrame) clipFrom = offsetFrame;
    if (clipTo > srcEnd) clipTo = srcEnd;
    if (clipTo <= clipFrom) return;

    clipFrom = std::min<size_t>(clipFrom, clip->frameCount());
    clipTo = std::min<size_t>(clipTo, clip->frameCount());
    if (clipTo <= clipFrom) return;

    const int imgX = static_cast<int>((tlFrom - m_scrollOffset) * pps);
    const int imgXEnd = static_cast<int>((tlTo - m_scrollOffset) * pps);
    int iw = std::max(1, imgXEnd - imgX);
    if (iw > width()) iw = width();

    const double dpr = devicePixelRatio();
    const int64_t visible = static_cast<int64_t>(clipTo - clipFrom);

    auto& cache = m_thumbnailCache[clip];
    if (cache.thumbnail.isNull()
        || cache.clipOffset != static_cast<int64_t>(clipFrom)
        || cache.visibleFrames != visible
        || cache.width != iw || cache.height != h
        || cache.devicePixelRatio != dpr
        || cache.pixelsPerSample != pps) {
        cache.thumbnail = renderAudioWindow(clip, clipFrom, clipTo, iw, h, dpr, pps,
                                            WaveformPainter::defaultColor());
        cache.clipOffset = static_cast<int64_t>(clipFrom);
        cache.visibleFrames = visible;
        cache.width = iw;
        cache.height = h;
        cache.devicePixelRatio = dpr;
        cache.pixelsPerSample = pps;
        if (m_thumbnailCache.size() > 128)
            m_thumbnailCache.clear();
    }
    if (!cache.thumbnail.isNull())
        painter.drawImage(imgX, y, cache.thumbnail);
}

QImage TrackViewWidget::renderAudioWindow(const std::shared_ptr<AudioClip>& clip,
                                          size_t clipFrom, size_t clipTo,
                                          int width, int height, double dpr, double pps,
                                          const QColor& color) {
    if (!clip || !clip->isValid() || clipTo <= clipFrom || width <= 0 || height <= 0)
        return QImage();

    const size_t visible = clipTo - clipFrom;

    // Raw sample access (in-memory buffer, or a decoded window for streaming).
    const float* rawData = nullptr;
    size_t rawFrameCount = 0;
    size_t rawOffset = 0;
    std::vector<float> window;
    if (pps >= vvvdaw::RawSampleRenderZoom) {
        if (!clip->isStreaming()) {
            rawData = clip->data();
            rawFrameCount = clip->frameCount();
            rawOffset = clipFrom;
        } else {
            size_t winStart = 0;
            if (!decodeStreamWindow(clip, clipFrom, clipTo, winStart, window))
                return QImage();
            size_t ch = static_cast<size_t>(clip->channels());
            if (ch == 0 || window.empty()) return QImage();
            rawData = window.data();
            rawFrameCount = window.size() / ch;
            rawOffset = clipFrom - winStart;
        }
        if (!rawData || rawOffset >= rawFrameCount)
            return QImage();

        // Deep zoom: every sample drawn as a distinct tick at its exact x.
        if (pps >= vvvdaw::SampleViewZoom)
            return WaveformPainter::renderSamplesPerSample(
                rawData, rawFrameCount, clip->channels(),
                rawOffset, visible, pps, width, height, dpr, color);

        // Moderate zoom: per-pixel min/max envelope from raw samples.
        return WaveformPainter::renderSamples(
            rawData, rawFrameCount, clip->channels(),
            rawOffset, visible, width, height, dpr, color);
    }

    // Envelope from peaks: fine level while it stays cheap per pixel, coarse
    // level for very zoomed-out views.
    const double fineMaxZoom = 1.0 / (AudioClip::FINE_PEAK_STEP_FRAMES * 8.0);
    if (pps >= fineMaxZoom && !clip->finePeaks().empty()) {
        return WaveformPainter::renderFromPeaks(
            clip->finePeaks().data(), clip->finePeaks().size(),
            AudioClip::FINE_PEAK_STEP_FRAMES, clip->frameCount(),
            clipFrom, visible, width, height, dpr, color);
    }
    if (clip->peaks().empty())
        return QImage();
    return WaveformPainter::renderFromPeaks(
        clip->peaks().data(), clip->peaks().size(),
        clip->peaksPerFrame(), clip->frameCount(),
        clipFrom, visible, width, height, dpr, color);
}

bool TrackViewWidget::decodeStreamWindow(const std::shared_ptr<AudioClip>& clip,
                                         size_t from, size_t to,
                                         size_t& winStart, std::vector<float>& samples) {
    if (!clip || !clip->isStreaming() || to <= from || to > clip->frameCount())
        return false;

    // Block-align the decoded window so small scrolls reuse the cache.
    constexpr size_t kBlockFrames = 4096;
    winStart = (from / kBlockFrames) * kBlockFrames;
    size_t winEnd = ((to + kBlockFrames - 1) / kBlockFrames) * kBlockFrames;
    if (winEnd > clip->frameCount()) winEnd = clip->frameCount();
    if (winEnd <= winStart) return false;

    auto& cache = m_decodeCache[clip];
    const size_t ch = static_cast<size_t>(clip->channels());
    if (cache.startFrame == winStart && cache.frameCount == winEnd - winStart
        && cache.samples.size() == (winEnd - winStart) * ch) {
        samples = cache.samples;
        return true;
    }

    if (!clip->readFrames(winStart, winEnd - winStart, cache.samples))
        return false;
    cache.startFrame = winStart;
    cache.frameCount = winEnd - winStart;
    samples = cache.samples;
    return true;
}

void TrackViewWidget::contextMenuEvent(QContextMenuEvent* event) {
    if (!m_track) {
        QWidget::contextMenuEvent(event);
        return;
    }

    int idx = -1;
    bool hit = (eventAtX(static_cast<int>(event->pos().x()), idx) >= 0);

    if (!hit) {
        if (isMidiMode()) {
            int64_t start = sampleAtX(static_cast<int>(event->pos().x()));
            if (m_snapToGrid)
                start = TimeUtils::snapSample(start, m_snapUnit);
            if (start < 0) start = 0;
            QMenu menu(this);
            QAction* addAction = menu.addAction("Add MIDI Event");
            connect(addAction, &QAction::triggered, this, [this, start] {
                emit addMidiEventRequested(start);
            });
            menu.exec(event->globalPos());
            return;
        }
        QWidget::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);

    // Right-click on an event inside a multi-selection keeps the selection;
    // right-click on an unselected event narrows the selection to just it.
    if (!eventIsSelected(idx)) {
        setSelection(idx);
    } else {
        m_selectionAnchorIndex = idx;
    }

    QAction* duplicateAction = menu.addAction("Duplicate");
    connect(duplicateAction, &QAction::triggered, this, [this, idx] {
        if (!m_track || idx < 0 || idx >= eventCount()) return;
        emit takeSwitchStarted();
        auto lock = m_project ? m_project->writeLock()
                              : std::unique_lock<std::shared_mutex>();
        if (isMidiMode()) {
            MidiEvent copy = m_track->midiEvents()[idx];
            copy.setStartSample(copy.startSample() +
                static_cast<int64_t>(vvvdaw::DefaultSnapUnitSamples));
            m_track->addMidiEvent(std::move(copy));
        } else {
            AudioEvent copy = m_track->events()[idx];
            copy.setStartSample(copy.startSample() +
                static_cast<int64_t>(vvvdaw::DefaultSnapUnitSamples));
            m_track->addEvent(std::move(copy));
        }
        setSelection(eventCount() - 1);
        m_thumbnailCache.clear();
        m_midiThumbCache.clear();
        m_decodeCache.clear();
        update();
        emit eventsChanged();
    });

    // Split the audio event at the mouse position. The request is recorded here
    // and emitted after menu.exec() returns: rebuilding tracks while the popup
    // (a child of this widget) is still executing would destroy this widget
    // from under the menu.
    if (!isMidiMode()) {
        int64_t cutSample = sampleAtX(static_cast<int>(event->pos().x()));
        int64_t evStart = eventStart(idx);
        if (cutSample > evStart && cutSample < evStart + eventDuration(idx)) {
            auto requestCut = [this, idx, cutSample](bool snapToGrid) {
                if (!m_track || idx < 0 || idx >= eventCount()) return;
                m_pendingCutEventId = eventIdAt(idx);
                m_pendingCutSample = cutSample;
                m_pendingCutSnap = snapToGrid;
            };
            QAction* cutAction = menu.addAction("Cut");
            connect(cutAction, &QAction::triggered, this, [requestCut] {
                requestCut(false);
            });
            QAction* snapCutAction = menu.addAction("Cut and Snap");
            connect(snapCutAction, &QAction::triggered, this, [requestCut] {
                requestCut(true);
            });
        }
    }

    QAction* deleteAction = menu.addAction("Delete");
    connect(deleteAction, &QAction::triggered, this, [this] {
        if (!m_track) return;
        emit takeSwitchStarted();
        deleteSelectedEvent();
    });

    // Crossfade the junctions between the selected audio events to hide the
    // discontinuities at their boundaries. Deferred until the popup closes
    // (the command rebuilds the tracks and would destroy the menu's parent).
    // A negative length asks the MainWindow for the default crossfade; 0
    // removes the fades.
    if (!isMidiMode() && m_selectedEventIds.size() >= 2) {
        QAction* crossfadeAction = menu.addAction("Crossfade Selected Events");
        connect(crossfadeAction, &QAction::triggered, this, [this] {
            m_pendingCrossfadeIds.assign(m_selectedEventIds.begin(),
                                         m_selectedEventIds.end());
            m_pendingCrossfadeSamples = -1;
            m_pendingCrossfade = true;
        });
        QAction* removeCrossfadeAction = menu.addAction("Remove Crossfades");
        connect(removeCrossfadeAction, &QAction::triggered, this, [this] {
            m_pendingCrossfadeIds.assign(m_selectedEventIds.begin(),
                                         m_selectedEventIds.end());
            m_pendingCrossfadeSamples = 0;
            m_pendingCrossfade = true;
        });
    }

    if (isMidiMode()) {
        QAction* pianoRollAction = menu.addAction("Open Piano Roll");
        connect(pianoRollAction, &QAction::triggered, this, [this, idx] {
            if (!m_track || idx < 0 || idx >= eventCount()) return;
            setSelection(idx);
            emit eventDoubleClicked(eventIdAt(idx));
        });
    }

    if (eventHasTakes(idx)) {
        menu.addSeparator();
        for (int i = 0; i < eventTakeCount(idx); ++i) {
            QString label = QString("Take %1").arg(i + 1);
            if (i == eventActiveTake(idx))
                label += " ✓";
            QAction* action = menu.addAction(label);
            connect(action, &QAction::triggered, this, [this, idx, i] {
                emit takeSwitchStarted();
                switchEventTake(idx, i);
                update();
            });
        }
    }

    menu.exec(event->globalPos());

    if (m_pendingCrossfade) {
        m_pendingCrossfade = false;
        std::vector<int64_t> ids = std::move(m_pendingCrossfadeIds);
        int64_t fadeSamples = m_pendingCrossfadeSamples;
        m_pendingCrossfadeIds.clear();
        m_pendingCrossfadeSamples = 0;
        if (ids.size() >= 2)
            emit crossfadeRequested(ids, fadeSamples);
    }

    if (m_pendingCutEventId >= 0) {
        int64_t eventId = m_pendingCutEventId;
        int64_t cutSample = m_pendingCutSample;
        bool snapToGrid = m_pendingCutSnap;
        m_pendingCutEventId = -1;
        m_pendingCutSnap = false;
        emit cutEventRequested(eventId, cutSample, snapToGrid);
    }
}
