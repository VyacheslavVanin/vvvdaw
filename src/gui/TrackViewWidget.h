#pragma once
#include <QWidget>
#include <QMap>
#include <memory>
#include <vector>
#include "core/Constants.h"
#include "model/AudioClip.h"
#include "model/AudioEvent.h"
#include "model/MidiEvent.h"

class Track;

class TrackViewWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrackViewWidget(Track* track, QWidget* parent = nullptr);

    void setTrack(Track* track) { m_track = track; }
    Track* track() const { return m_track; }

    void setScrollOffset(int64_t offset);
    int64_t scrollOffset() const { return m_scrollOffset; }

    void setZoom(double pixelsPerSample);
    double zoom() const { return m_pixelsPerSample; }

    void setPlayheadPosition(int64_t sample) { m_playheadPos = sample; update(); }
    int64_t playheadPosition() const { return m_playheadPos; }

    // While recording, draw a growing rectangle showing the extent of the
    // audio being recorded on this (record-armed) track. `endSample` clamps the
    // rectangle to a record region, or -1 to let it grow with the playhead.
    void setRecordingPreview(bool active, int64_t startSample, int64_t endSample = -1);
    bool recordingPreviewActive() const { return m_recordingActive; }

    // Live waveform peaks (from the recording writer thread) rendered inside
    // the recording preview rectangle. Pass empty to clear.
    void setRecordingPeaks(std::vector<AudioClip::Peak> peaks, int64_t framesPerPeak,
                           int64_t recordedFrames);

    void updateFromTrack();
    void deleteSelectedEvent();
    int selectedEventIndex() const { return m_selectedEventIndex; }
    void setAlternateRow(bool alternate) { m_alternateRow = alternate; update(); }
    void setDragPreview(const AudioEvent* event, int64_t startSample);
    void setMidiDragPreview(const MidiEvent* event, int64_t startSample);
    void clearDragPreview();
    void setDragSourceVisible(bool visible) {
        if (m_dragSourceVisible != visible) { m_dragSourceVisible = visible; update(); }
    }

    void setSnapToGrid(bool snap) { m_snapToGrid = snap; }
    void setSnapUnit(double samples) { m_snapUnit = samples; }
    void setSamplesPerTick(double samplesPerTick) { m_samplesPerTick = samplesPerTick; }

    // Horizontal mouse position (pixels) used to draw the thin cursor line,
    // or -1 while the mouse is outside the widget.
    int mouseCursorX() const { return m_mouseX; }

signals:
    void scrollOffsetChanged(int64_t offset);
    void eventMoved(int64_t eventId, int64_t newStartSample);
    void eventsChanged();
    void eventDragFinished(int64_t eventId, int64_t newStartSample, QPoint globalPos);
    void dragInProgress(int64_t eventId, int64_t currentStartSample, QPoint globalPos);
    void eventDragStarted();
    void eventEdgeTrimStarted();
    void zoomChanged(double zoom);
    void takeSwitchStarted();
    void eventDoubleClicked(int64_t eventId);
    void addMidiEventRequested(int64_t startSample);
    void cutEventRequested(int64_t eventId, int64_t cutSample);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class EdgeDrag { None, Left, Right };

    int64_t sampleAtX(int x) const;
    int eventAtX(int x, int& eventIndex);
    EdgeDrag edgeAtX(int x, int eventIndex) const;
    void renderThumbnail(QPainter& painter, const std::shared_ptr<AudioClip>& clip,
                         int64_t eventStartSample, int64_t eventDuration,
                         size_t offsetFrame, size_t sourceFrames,
                         int y, int h);
    QImage renderAudioWindow(const std::shared_ptr<AudioClip>& clip,
                             size_t clipFrom, size_t clipTo,
                             int width, int height, double dpr, double pps,
                             const QColor& color);
    bool decodeStreamWindow(const std::shared_ptr<AudioClip>& clip,
                            size_t from, size_t to,
                            size_t& winStart, std::vector<float>& samples);
    void renderMidiPreview(QPainter& painter, const std::shared_ptr<MidiClip>& clip,
                           int64_t offsetSample, int64_t durationSample,
                           int x, int y, int w, int h);

    bool isMidiMode() const;
    int eventCount() const;
    int64_t eventStart(int index) const;
    int64_t eventOffset(int index) const;
    int64_t eventDuration(int index) const;
    int64_t eventIdAt(int index) const;
    bool eventHasTakes(int index) const;
    int eventActiveTake(int index) const;
    int eventTakeCount(int index) const;
    void setEventStart(int index, int64_t value);
    void setEventOffset(int index, int64_t value);
    void setEventDuration(int index, int64_t value);
    void switchEventTake(int index, int takeIndex);
    std::shared_ptr<MidiClip> midiClipAt(int index) const;

    Track* m_track = nullptr;
    int64_t m_scrollOffset = 0;
    double m_pixelsPerSample = vvvdaw::DefaultZoom;
    int64_t m_playheadPos = -1;

    // Live audio recording preview (growing rectangle).
    bool m_recordingActive = false;
    int64_t m_recordStartSample = 0;
    int64_t m_recordEndSample = -1;
    std::vector<AudioClip::Peak> m_recordingPeaks;
    int64_t m_recordingFramesPerPeak = 0;
    int64_t m_recordingRecordedFrames = 0;

    // Cached image of the viewport-visible recording window, rendered at true
    // sample positions so chunks are appended rather than stretched.
    struct RecordingWaveCache {
        QImage image;
        int64_t peakCount = -1;
        int64_t offsetFrame = -1;
        int64_t visibleFrames = -1;
        int width = -1;
        int height = -1;
        double devicePixelRatio = -1;
    };
    RecordingWaveCache m_recordingWaveCache;

    struct ClipCache {
        QImage thumbnail;
        int64_t clipOffset = -1;
        int64_t visibleFrames = -1;
        int width = -1;
        int height = -1;
        double devicePixelRatio = -1;
        double pixelsPerSample = -1;
    };
    QMap<std::shared_ptr<AudioClip>, ClipCache> m_thumbnailCache;

    // Decoded visible window of a streaming clip (block-aligned, reused while
    // the view stays within the same block).
    struct DecodeCache {
        size_t startFrame = 0;
        size_t frameCount = 0;
        std::vector<float> samples;
    };
    QMap<std::shared_ptr<AudioClip>, DecodeCache> m_decodeCache;

    struct MidiThumbCache {
        QImage image;
        int64_t revision = -1;
        int64_t offsetSample = 0;
        int64_t durationSample = 0;
        double samplesPerTick = 0.0;
    };
    QMap<std::shared_ptr<MidiClip>, MidiThumbCache> m_midiThumbCache;

    // Drag state
    bool m_dragging = false;
    int m_dragEventIndex = -1;
    int64_t m_dragStartSample = 0;
    int m_dragStartMouseX = 0;

    // Hover
    int m_hoverEventIndex = -1;

    // Mouse cursor line (thin vertical line at the current mouse X).
    int m_mouseX = -1;

    // Selection
    int m_selectedEventIndex = -1;

    // Row appearance
    bool m_alternateRow = false;
    bool m_dragSourceVisible = true;

    // Edge trim state
    EdgeDrag m_edgeDrag = EdgeDrag::None;
    int m_edgeDragEventIndex = -1;
    int64_t m_edgeDragStartOffset = 0;
    int64_t m_edgeDragStartDuration = 0;
    int64_t m_edgeDragStartSample = 0;
    int64_t m_edgeDragStartMouseSample = 0;

    static constexpr int EdgeHandleWidth = 6;

    struct DragPreview {
        const AudioEvent* audioEvent = nullptr;
        const MidiEvent* midiEvent = nullptr;
        int64_t startSample = 0;
    };
    DragPreview m_dragPreview;

    bool m_snapToGrid = true;
    double m_snapUnit = vvvdaw::DefaultSnapUnitSamples;
    double m_samplesPerTick = 25.0;

    // Middle-button drag panning state.
    bool m_panning = false;
    int m_panStartX = 0;
    int64_t m_panStartOffset = 0;

    // Cut requested from the context menu; emitted after the popup closes.
    int64_t m_pendingCutEventId = -1;
    int64_t m_pendingCutSample = 0;
};
