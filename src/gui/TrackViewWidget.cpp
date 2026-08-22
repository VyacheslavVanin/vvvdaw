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
#include <cmath>

TrackViewWidget::TrackViewWidget(Track* track, QWidget* parent)
    : QWidget(parent)
    , m_track(track)
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
    update();
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

    int ex = static_cast<int>((eventStart(eventIndex) - m_scrollOffset) * m_pixelsPerSample);
    int ew = static_cast<int>(eventDuration(eventIndex) * m_pixelsPerSample);

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

    int n = eventCount();
    for (int i = 0; i < n; ++i) {
        if (!eventHasTakes(i) && !isMidiMode()) {
            auto& ev = m_track->events()[i];
            if (!ev.clip() || !ev.clip()->isValid()) continue;
        }

        int x = static_cast<int>((eventStart(i) - m_scrollOffset) * m_pixelsPerSample);
        int w = static_cast<int>(eventDuration(i) * m_pixelsPerSample);
        if (x + w < 0 || x > width()) continue;

        QRect eventRect(x, 2, w, trackHeight - 4);

        bool isHovered = (i == m_hoverEventIndex);
        bool isDragged = (i == m_dragEventIndex && m_dragging);
        bool isSelected = (i == m_selectedEventIndex);
        if (isDragged && !m_dragSourceVisible) continue;

        QColor bgColor = isSelected ? QColor("#334466")
                       : (isHovered ? QColor("#224466") : QColor("#1a3344"));
        QColor borderColor = isDragged ? QColor("#ffcc00")
                           : (isSelected ? QColor("#ffaa00")
                           : (m_track->isMuted() ? QColor("#666") : QColor("#88ccff")));

        painter.setPen(QPen(borderColor, (isDragged || isSelected) ? 2 : 1));
        painter.setBrush(bgColor);
        painter.drawRect(eventRect);

        if (isMidiMode()) {
            auto clip = midiClipAt(i);
            if (clip) {
                int th = eventRect.height() - 2;
                renderMidiPreview(painter, clip, eventOffset(i), eventDuration(i),
                                  eventRect.x() + 1, eventRect.y() + 1, w, th);
            }
        } else {
            int th = eventRect.height() - 2;
            auto& ev = m_track->events()[i];
            renderThumbnail(painter, ev.clip(),
                            ev.offsetSample(), ev.sourceFrames(),
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
        if (eventTakeCount(i) > 1) {
            painter.setPen(QColor("#aaddff"));
            QFont takeFont = painter.font();
            takeFont.setPixelSize(9);
            painter.setFont(takeFont);
            painter.drawText(eventRect.x() + 3, eventRect.y() + 12,
                QString("T%1/%2").arg(eventActiveTake(i) + 1).arg(eventTakeCount(i)));
        }
    }

    // Drag preview on target track
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
        int x = static_cast<int>((m_dragPreview.startSample - m_scrollOffset) * m_pixelsPerSample);
        int w = static_cast<int>(m_dragPreview.audioEvent->durationSample() * m_pixelsPerSample);
        if (x + w >= 0 && x <= width()) {
            QRect eventRect(x, 2, w, trackHeight - 4);

            painter.setPen(QPen(QColor("#ffcc00"), 2));
            painter.setBrush(QColor("#1a3344"));
            painter.drawRect(eventRect);

            {
                auto clip = m_dragPreview.audioEvent->clip();
                int th = eventRect.height() - 2;
                renderThumbnail(painter, clip,
                                m_dragPreview.audioEvent->offsetSample(), m_dragPreview.audioEvent->sourceFrames(),
                                eventRect.x() + 1, eventRect.y() + 1, w, th);
            }

            painter.setPen(QPen(QColor("#ffcc00"), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(eventRect);
        }
    }

    // Live audio recording preview: a rectangle growing with the playhead so
    // it is visible that recording is in progress before the event is written.
    if (m_recordingActive && m_track && m_track->isRecordArmed()
        && m_track->type() == Track::Type::Audio) {
        int64_t end = m_playheadPos;
        if (m_recordEndSample >= 0 && end > m_recordEndSample)
            end = m_recordEndSample;
        if (end > m_recordStartSample) {
            int rx = static_cast<int>((m_recordStartSample - m_scrollOffset) * m_pixelsPerSample);
            int rw = static_cast<int>((end - m_recordStartSample) * m_pixelsPerSample);
            if (rx + rw >= 0 && rx <= width()) {
                QRect recRect(rx, 2, rw, trackHeight - 4);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 180, 100, 55));
                painter.drawRect(recRect);

                // Live waveform of the audio captured so far, drawn at true sample
                // positions: each refresh appends a new chunk to the right and
                // the not-yet-recorded part of the rectangle stays empty. Only
                // the viewport-visible window is rendered, re-rendered when new
                // peaks arrive or the view scrolls/zooms.
                if (!m_recordingPeaks.empty() && m_recordingFramesPerPeak > 0
                    && m_recordingRecordedFrames > 0) {
                    const int64_t recStart = m_recordStartSample;
                    const int64_t recFrames = m_recordingRecordedFrames;
                    const int64_t viewLeft = m_scrollOffset;
                    const int64_t viewRight = sampleAtX(width());
                    const int64_t wl = std::max<int64_t>(0, viewLeft - recStart);
                    const int64_t wr = std::min<int64_t>(recFrames, viewRight - recStart);
                    if (wr > wl) {
                        const int ih = std::max(1, trackHeight - 6);
                        const int iw = std::max(1, static_cast<int>(
                            std::llround(static_cast<double>(wr - wl) * m_pixelsPerSample)));
                        const int imgX = static_cast<int>(std::llround(
                            static_cast<double>(recStart + wl - m_scrollOffset) * m_pixelsPerSample));
                        auto& cache = m_recordingWaveCache;
                        if (cache.image.isNull()
                            || cache.peakCount != static_cast<int64_t>(m_recordingPeaks.size())
                            || cache.offsetFrame != wl
                            || cache.visibleFrames != (wr - wl)
                            || cache.width != iw || cache.height != ih) {
                            cache.image = WaveformPainter::renderFromPeaks(
                                m_recordingPeaks.data(), m_recordingPeaks.size(),
                                static_cast<size_t>(m_recordingFramesPerPeak),
                                static_cast<size_t>(recFrames),
                                static_cast<size_t>(wl), static_cast<size_t>(wr - wl),
                                iw, ih, WaveformPainter::recordingColor());
                            cache.peakCount = static_cast<int64_t>(m_recordingPeaks.size());
                            cache.offsetFrame = wl;
                            cache.visibleFrames = wr - wl;
                            cache.width = iw;
                            cache.height = ih;
                        }
                        if (!cache.image.isNull())
                            painter.drawImage(imgX, 3, cache.image);
                    }
                }

                painter.setPen(QPen(QColor("#00d06a"), 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(recRect);
            }
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
        int64_t start = eventStart(m_dragEventIndex);
        int phx = static_cast<int>((start - m_scrollOffset) * m_pixelsPerSample);
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setPointSize(9);
        painter.setFont(f);
        painter.drawText(phx + 4, 14, QString("Sample: %1").arg(start));
    }
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

void TrackViewWidget::wheelEvent(QWheelEvent* event) {
    int deltaY = static_cast<int>(event->angleDelta().y());
    if (event->modifiers() & Qt::ControlModifier && deltaY != 0) {
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
    } else {
        // Plain wheel: let the enclosing scroll area handle it (scrolls the
        // track list vertically). Horizontal panning is done by middle-drag.
        event->ignore();
        return;
    }
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
                m_selectedEventIndex = idx;
                m_edgeDrag = edge;
                m_edgeDragEventIndex = idx;
                m_edgeDragStartOffset = eventOffset(idx);
                m_edgeDragStartDuration = eventDuration(idx);
                m_edgeDragStartSample = eventStart(idx);
                m_edgeDragStartMouseSample = sampleAtX(static_cast<int>(event->position().x()));
                setCursor(Qt::SizeHorCursor);
                emit eventEdgeTrimStarted();
                update();
                return;
            }

            m_selectedEventIndex = idx;

            bool ctrlDrag = (event->modifiers() & Qt::ControlModifier);
            bool shiftDrag = (event->modifiers() & Qt::ShiftModifier);
            bool duplicate = ctrlDrag || (isMidiMode() && shiftDrag);
            if (duplicate) {
                emit eventDragStarted();
                if (isMidiMode()) {
                    MidiEvent copy = shiftDrag ? m_track->midiEvents()[idx].cloneDeep()
                                               : m_track->midiEvents()[idx];
                    copy.setStartSample(copy.startSample() +
                        static_cast<int64_t>(vvvdaw::DefaultSnapUnitSamples));
                    m_track->addMidiEvent(std::move(copy));
                    m_selectedEventIndex = eventCount() - 1;
                } else {
                    AudioEvent copy = m_track->events()[idx];
                    copy.setStartSample(copy.startSample() +
                        static_cast<int64_t>(vvvdaw::DefaultSnapUnitSamples));
                    m_track->addEvent(std::move(copy));
                    m_selectedEventIndex = eventCount() - 1;
                }
            }

            m_dragEventIndex = m_selectedEventIndex;
            m_dragging = true;
            m_dragStartSample = eventStart(m_selectedEventIndex);
            m_dragStartMouseX = static_cast<int>(event->position().x());
            setCursor(Qt::ClosedHandCursor);

            if (!duplicate)
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
            m_edgeDrag = EdgeDrag::None;
            m_edgeDragEventIndex = -1;
            unsetCursor();
            if (m_track && m_selectedEventIndex >= 0 && m_selectedEventIndex < eventCount())
                emit eventsChanged();
            update();
            return;
        }

        if (m_dragging) {
            m_dragging = false;
            unsetCursor();
            if (m_track && m_dragEventIndex >= 0) {
                int64_t id = eventIdAt(m_dragEventIndex);
                int64_t start = eventStart(m_dragEventIndex);
                emit eventMoved(id, start);
                emit eventDragFinished(id, start, event->globalPosition().toPoint());
            }
            m_dragEventIndex = -1;
            update();
        }
    }
}

void TrackViewWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isMidiMode()) {
        int idx = -1;
        if (eventAtX(static_cast<int>(event->position().x()), idx) >= 0) {
            m_selectedEventIndex = idx;
            emit eventDoubleClicked(eventIdAt(idx));
            update();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
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
    if (!m_track || m_selectedEventIndex < 0 || m_selectedEventIndex >= eventCount())
        return;

    int64_t id = eventIdAt(m_selectedEventIndex);
    if (isMidiMode())
        m_track->removeMidiEvent(id);
    else
        m_track->removeEvent(id);
    m_selectedEventIndex = -1;
    m_thumbnailCache.clear();
    m_midiThumbCache.clear();
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

    QAction* duplicateAction = menu.addAction("Duplicate");
    connect(duplicateAction, &QAction::triggered, this, [this, idx] {
        if (!m_track || idx < 0 || idx >= eventCount()) return;
        emit takeSwitchStarted();
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
        m_selectedEventIndex = eventCount() - 1;
        m_thumbnailCache.clear();
        m_midiThumbCache.clear();
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

    if (isMidiMode()) {
        QAction* pianoRollAction = menu.addAction("Open Piano Roll");
        connect(pianoRollAction, &QAction::triggered, this, [this, idx] {
            if (!m_track || idx < 0 || idx >= eventCount()) return;
            m_selectedEventIndex = idx;
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
}
