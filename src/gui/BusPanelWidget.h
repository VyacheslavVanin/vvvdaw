#pragma once
#include <QWidget>
#include <QColor>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QTimer>
#include <QPoint>
#include <utility>
#include <vector>

class Project;
struct AudioBus;
class PluginInstance;
class PluginListWidget;
class PluginManager;
class AudioEngine;
class BusLevelMeter;
class BusSendsWidget;
class BusColorBar;
class QFrame;
class QMouseEvent;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

class BusPanelWidget : public QScrollArea {
    Q_OBJECT
public:
    explicit BusPanelWidget(Project& project, QWidget* parent = nullptr);

    void rebuild();
    void refreshOutCombos();
    // Re-read every strip's effective color and repaint (after a color change
    // or undo/redo that did not rebuild the whole panel).
    void refreshColors();
    void setPluginManager(PluginManager* pm) { m_pluginManager = pm; }
    void setAudioEngine(AudioEngine* engine) { m_engine = engine; }
    void setAudioParams(double sampleRate, int bufferSize) { m_sampleRate = sampleRate; m_bufferSize = bufferSize; }
    std::vector<int> selectedBusIndices() const { return m_selected; }
    // Apply a bus drag & drop ending at `pos` (container coordinates) with the
    // given dragged bus indices (moves/reorders; exposed for tests).
    void handleBusDrop(const QPoint& pos, const std::vector<int>& dragged);

    static constexpr int kControlsWidth = 68;
    static constexpr int kPluginPanelWidth = 240;

    // A color change requested through a strip's color bar. `newSet == false`
    // clears the manual color (restoring the automatic one). When
    // `overrideChildren` is true the same color is applied to all of the bus's
    // descendants, overriding any manual colors they had.
    struct BusColorChange {
        int busIndex = -1;
        QColor oldColor;
        QColor newColor;
        bool oldSet = false;
        bool newSet = false;
        bool overrideChildren = false;
    };

signals:
    void busChanged();
    void addBusRequested();
    void removeBusRequested(int index);
    void openBusPluginEditorRequested(int busIndex, PluginInstance* plugin);
    void busPluginWillBeRemoved(PluginInstance* plugin);
    void busPluginAddRequested(int busIndex, const QString& type, const QString& path);
    void busPluginRemoved(int busIndex, int pluginIndex);
    void busPluginWillBeMoved(int busIndex, int from, int to);
    void busPluginWillBeToggled(int busIndex);
    void busVolumeWillChange(int busIndex, float oldVal, float newVal);
    void busPanWillChange(int busIndex, float oldVal, float newVal);
    void busSoloWillChange(int busIndex, bool oldVal, bool newVal);
    void busMuteWillChange(int busIndex, bool oldVal, bool newVal);
    void busNameWillChange(int busIndex, const QString& oldName, const QString& newName);
    void busOutputWillChange(int busIndex, int oldVal, int newVal);
    void busSendAddRequested(int busIndex);
    void busSendRemoveRequested(int busIndex, int sendIndex);
    void busSendTargetWillChange(int busIndex, int sendIndex, int oldBus, int newBus);
    void busSendLevelWillChange(int busIndex, int sendIndex, float oldLevel, float newLevel);
    void busSendPreWillChange(int busIndex, int sendIndex, bool oldPre, bool newPre);
    // A drag / "put to folder" moved the given buses: the panel reordered and/or
    // re-routed them. Old/new display order and old/new parent (outputBusIndex)
    // per affected bus let the undo command restore the state.
    void busesMoved(std::vector<int> oldOrder, std::vector<int> newOrder,
                    std::vector<std::pair<int, int>> oldParents,
                    std::vector<std::pair<int, int>> newParents);
    void busFolderCollapseWillChange(int busIndex, bool oldVal, bool newVal);
    void createBusFolderRequested(const QString& name, const std::vector<int>& children);
    void busColorWillChange(const BusColorChange& change);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    struct BusRow {
        QWidget* widget = nullptr;
        QWidget* controls = nullptr;
        QLineEdit* nameEdit = nullptr;
        QPushButton* soloButton = nullptr;
        QPushButton* muteButton = nullptr;
        QComboBox* outCombo = nullptr;
        QSlider* panSlider = nullptr;
        QSlider* volumeSlider = nullptr;
        BusLevelMeter* levelMeter = nullptr;
        QPushButton* folderToggle = nullptr;
        QPushButton* panelToggle = nullptr;
        // Combined plugins + sends panel (plugins on top, sends below),
        // revealed by the single panelToggle.
        QWidget* fxPanel = nullptr;
        PluginListWidget* pluginList = nullptr;
        BusSendsWidget* sendsList = nullptr;
        BusColorBar* colorBar = nullptr;
    };

    void updateMeters();
    void buildBusStrip(int busIndex);
    // Render one bus and, when it is an unfolded folder, its children.
    void renderBusTree(int busIndex);
    // Background color of a strip: the bus's effective color.
    QColor stripBaseColor(int busIndex) const;
    // Selection.
    bool isSelected(int busIndex) const;
    void setSelected(const std::vector<int>& buses);
    void toggleSelected(int busIndex);
    void handleStripClick(int busIndex, Qt::KeyboardModifiers modifiers);
    void updateSelectionStyles();
    int busIndexForWidget(QWidget* widget) const;
    // Where a drop at `pos` (container coordinates) would insert the dragged
    // buses: the new parent bus, the display-order index to insert before
    // (-1 = append) and the x position for the insertion line. When the drop
    // targets a folder's body, `highlightFolder` is that folder and the parent
    // is it (the dragged buses move into the folder instead of being inserted).
    struct DropSlot {
        int parent = 0;
        int beforeIndex = -1;
        int insertionX = 0;
        int highlightFolder = -1;
    };
    DropSlot dropSlotAt(const QPoint& pos) const;
    int nextRenderIndex(int afterIndex) const;
    // Drag & drop.
    void startBusDrag(int busIndex);
    // Re-route the given buses into `folder` (a bus index) without changing the
    // display order; used by the "Put to folder" context menu.
    void moveBusesToFolder(const std::vector<int>& targets, int folder);

    // eventFilter() sub-handlers, split out per event type.
    bool handleNameDoubleClick(QObject* obj);
    bool handleMousePress(QObject* obj, QMouseEvent* event);
    bool handleMouseMove(QObject* obj, QMouseEvent* event);
    bool handleContextMenu(QObject* obj, QContextMenuEvent* event);
    // Drag handlers return true when the dragged payload is a bus drag.
    bool handleDragEnter(QDragEnterEvent* event);
    bool handleDragMove(QDragMoveEvent* event);
    bool handleDrop(QDropEvent* event);

    Project& m_project;
    PluginManager* m_pluginManager = nullptr;
    AudioEngine* m_engine = nullptr;
    double m_sampleRate = 48000;
    int m_bufferSize = 512;
    QWidget* m_container = nullptr;
    QHBoxLayout* m_containerLayout = nullptr;
    std::vector<BusRow> m_busRows;
    // Per-bus "plugins + sends panel open" state, carried across rebuild() so a
    // full panel refresh (e.g. after adding a plugin or send) does not collapse
    // an explicitly opened combined panel.
    std::vector<bool> m_panelOpen;
    // Flat render order of the panel (bus indices in display sequence, folders
    // expanded), used for shift-range selection.
    std::vector<int> m_renderOrder;
    std::vector<int> m_selected;
    int m_selectionAnchor = -1;
    int m_dragSource = -1;
    QPoint m_dragStartPos;
    QFrame* m_insertionLine = nullptr;
    QFrame* m_folderHighlight = nullptr;
    QTimer* m_meterTimer = nullptr;
};
