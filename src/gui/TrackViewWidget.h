#pragma once
#include <QWidget>
#include <QMap>
#include <memory>
#include "core/Constants.h"
#include "model/AudioClip.h"
#include "model/AudioEvent.h"

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

    void updateFromTrack();
    void deleteSelectedEvent();
    int selectedEventIndex() const { return m_selectedEventIndex; }
    void setAlternateRow(bool alternate) { m_alternateRow = alternate; update(); }
    void setDragPreview(const AudioEvent* event, int64_t startSample);
    void setDragSourceVisible(bool visible) {
        if (m_dragSourceVisible != visible) { m_dragSourceVisible = visible; update(); }
    }

    void setSnapToGrid(bool snap) { m_snapToGrid = snap; }
    void setSnapUnit(double samples) { m_snapUnit = samples; }

signals:
    void scrollOffsetChanged(int64_t offset);
    void eventMoved(int64_t eventId, int64_t newStartSample);
    void eventsChanged();
    void eventDragFinished(int64_t eventId, int64_t newStartSample, QPoint globalPos);
    void dragInProgress(int64_t eventId, int64_t currentStartSample, QPoint globalPos);
    void eventDragStarted();
    void zoomChanged(double zoom);
    void takeSwitchStarted();

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    int64_t sampleAtX(int x) const;
    AudioEvent* eventAtX(int x, int& eventIndex);
    void renderThumbnail(QPainter& painter, const std::shared_ptr<AudioClip>& clip,
                        int x, int y, int w, int h);

    Track* m_track = nullptr;
    int64_t m_scrollOffset = 0;
    double m_pixelsPerSample = vvvdaw::DefaultZoom;
    int64_t m_playheadPos = -1;

    struct ClipCache {
        QImage thumbnail;
        int64_t frameCount = 0;
    };
    QMap<std::shared_ptr<AudioClip>, ClipCache> m_thumbnailCache;

    // Drag state
    bool m_dragging = false;
    int m_dragEventIndex = -1;
    int64_t m_dragStartSample = 0;
    int m_dragStartMouseX = 0;

    // Hover
    int m_hoverEventIndex = -1;

    // Selection
    int m_selectedEventIndex = -1;

    // Row appearance
    bool m_alternateRow = false;
    bool m_dragSourceVisible = true;

    struct DragPreview {
        const AudioEvent* event = nullptr;
        int64_t startSample = 0;
    };
    DragPreview m_dragPreview;

    bool m_snapToGrid = true;
    double m_snapUnit = vvvdaw::DefaultSnapUnitSamples;
};
