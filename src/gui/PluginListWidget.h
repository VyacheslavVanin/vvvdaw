#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QPoint>
#include <QPointF>
#include <QFrame>
#include <vector>

class QLayoutItem;
class PluginChain;
class PluginInstance;
class PluginManager;
class Track;
struct AudioBus;
class Instrument;
class Project;

class PluginListWidget : public QWidget {
    Q_OBJECT
public:
    explicit PluginListWidget(QWidget* parent = nullptr);

    void setTrack(Track* track);
    void setBus(AudioBus* bus);
    void setInstrument(Instrument* instrument);
    void setPluginManager(PluginManager* pm) { m_pluginManager = pm; }
    void setAudioParams(double sampleRate, int bufferSize) { m_sampleRate = sampleRate; m_bufferSize = bufferSize; }
    void setInstrumentsOnly(bool only) { m_instrumentsOnly = only; }
    // Enables the sidechain assignment UI for bus plugin lists: the list needs
    // the project (to resolve sidechain sends across buses) and the bus index
    // owning this chain.
    void setProject(Project* project, int ownerBusIndex);
    // Optional caption shown in the header row next to the "+" button (e.g.
    // "effects:"). Empty text hides the label.
    void setHeaderLabel(const QString& text);
    void rebuild();

signals:
    void pluginRemoved(int index);
    void pluginWillBeRemoved(PluginInstance* plugin);
    void pluginWillBeMoved(int from, int to);
    void openEditorRequested(PluginInstance* plugin);
    void pluginWillBeToggled();
    void pluginAddRequested(const QString& type, const QString& path);
    void scanRequested();
    void sidechainEditRequested(PluginInstance* plugin);
    void sidechainClearRequested(PluginInstance* plugin);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onAddClicked();
    void onRemoveClicked(int index);

private:
    void buildRow(PluginInstance* plugin, int index);
    PluginChain* targetChain() const;
    // True when the plugin lists a sidechain input and this list is a bus list
    // with a project attached (i.e. sidechain assignment is possible).
    bool sidechainSupported(PluginInstance* plugin) const;
    // True when a sidechain send for this plugin already exists on some bus.
    bool sidechainAssigned(PluginInstance* plugin) const;
    int rowAtPos(const QPoint& pos) const;
    // Drag & drop insertion point (boundary index 0..count, in container
    // coordinates) and the Y position of the indicator line for that boundary.
    int insertionIndexAt(int y) const;
    int insertionLineY(int index) const;
    void updateInsertionLine(const QDropEvent* event);

    Track* m_track = nullptr;
    AudioBus* m_bus = nullptr;
    Instrument* m_instrument = nullptr;
    bool m_instrumentsOnly = false;
    Project* m_project = nullptr;
    int m_ownerBusIndex = -1;
    PluginManager* m_pluginManager = nullptr;
    double m_sampleRate = 48000;
    int m_bufferSize = 512;

    QVBoxLayout* m_mainLayout = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_container = nullptr;
    QVBoxLayout* m_containerLayout = nullptr;
    QLabel* m_headerLabel = nullptr;
    QPushButton* m_addButton = nullptr;

    std::vector<QWidget*> m_rows;
    int m_dragFromIndex = -1;
    QPointF m_dragStartPos;
    // Thin horizontal line marking the plugin insertion point during a drag.
    QFrame* m_insertionLine = nullptr;
    // Spacer absorbing surplus vertical space so plugin rows keep a compact
    // height instead of stretching to fill the list.
    QLayoutItem* m_trailingStretch = nullptr;
};
