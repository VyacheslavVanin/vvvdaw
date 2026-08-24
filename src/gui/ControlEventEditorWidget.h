#pragma once
#include <QWidget>
#include <QPoint>
#include "commands/MidiCommands.h"
#include "model/MidiControlEvent.h"
#include <cstdint>
#include <map>
#include <vector>

class Project;
class UndoStack;
class MidiClip;
class MidiEvent;

// Automation lane for CC / pitch-bend control events of one MidiClip. Values
// are point events: click / drag to draw (interpolated across the sweep),
// right-click removes the event under the cursor. Edits are committed as a
// single EditControlEventsCommand on release so a whole gesture is one undo
// step.
class ControlEventEditorWidget : public QWidget {
    Q_OBJECT
public:
    ControlEventEditorWidget(Project& project, UndoStack& undo,
                             int trackIndex, int64_t eventId, QWidget* parent = nullptr);

    bool reload();
    void setPixelsPerTick(double p);
    void setSnapDiv(int div);

    // Lane selection. Pass kind = PitchBend to edit pitch bend (number is
    // ignored); ControlChange with a CC number edits that controller.
    MidiControlEvent::Kind kind() const { return m_kind; }
    uint8_t number() const { return m_number; }
    void setLane(MidiControlEvent::Kind kind, uint8_t number);

    // Highest value of the selected lane's domain (127 for CC, 16383 for pitch
    // bend) — used by the window to scale the value label.
    int laneMax() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void controlEventsChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    static constexpr int kKeysWidth = 56;
    static constexpr int kLaneHeight = 80;

    MidiClip* clip() const;
    MidiEvent* currentEvent() const;
    int tickToX(int64_t tick) const;
    int64_t xToTick(int x) const;
    int64_t snapTickFloor(int64_t tick) const;
    bool snapEnabled() const;
    int valueFromY(int y) const;
    int yFromValue(int value) const;

    MidiControlEvent* eventAtTick(int64_t tick) const;
    int64_t nearestEventTick(int x) const;
    // Record (add/update) the value at a snapped tick, interpolating as the
    // sweep crosses ticks between the previous and current cursor positions.
    void paintTo(int x, int y, int64_t lastTick, int lastValue);
    void beginDrag(const QPoint& pos);
    void updateDrag(const QPoint& pos);
    void endDrag();
    void removeAt(const QPoint& pos);

    Project& m_project;
    UndoStack& m_undo;
    int m_trackIndex;
    int64_t m_eventId;

    double m_pixelsPerTick = 0.06;
    int m_snapDiv = 4;

    MidiControlEvent::Kind m_kind = MidiControlEvent::Kind::ControlChange;
    uint8_t m_number = 1; // default lane: modulation wheel

    bool m_dragging = false;
    int m_lastTick = -1;
    int m_lastValue = 0;
    std::vector<ControlEventChange> m_changes;
    std::map<int64_t, size_t> m_changeIndexByTick;
};