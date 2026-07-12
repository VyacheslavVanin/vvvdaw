#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QPoint>
#include <QPointF>
#include <vector>

class PluginChain;
class PluginInstance;
class PluginManager;
class Track;
struct AudioBus;

class PluginListWidget : public QWidget {
    Q_OBJECT
public:
    explicit PluginListWidget(QWidget* parent = nullptr);

    void setTrack(Track* track);
    void setBus(AudioBus* bus);
    void setPluginManager(PluginManager* pm) { m_pluginManager = pm; }
    void setAudioParams(double sampleRate, int bufferSize) { m_sampleRate = sampleRate; m_bufferSize = bufferSize; }
    void rebuild();

signals:
    void pluginAdded(int index);
    void pluginRemoved(int index);
    void pluginWillBeRemoved(PluginInstance* plugin);
    void pluginMoved(int from, int to);
    void openEditorRequested(PluginInstance* plugin);
    void scanRequested();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onAddClicked();
    void onRemoveClicked(int index);

private:
    void buildRow(PluginInstance* plugin, int index);
    PluginChain* targetChain() const;
    int rowAtPos(const QPoint& pos) const;

    Track* m_track = nullptr;
    AudioBus* m_bus = nullptr;
    PluginManager* m_pluginManager = nullptr;
    double m_sampleRate = 48000;
    int m_bufferSize = 512;

    QVBoxLayout* m_mainLayout = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_container = nullptr;
    QVBoxLayout* m_containerLayout = nullptr;
    QPushButton* m_addButton = nullptr;

    std::vector<QWidget*> m_rows;
    int m_dragFromIndex = -1;
    QPointF m_dragStartPos;
};
