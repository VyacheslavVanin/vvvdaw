#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
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
    void rebuild();

signals:
    void pluginAdded(int index);
    void pluginRemoved(int index);
    void pluginMoved(int from, int to);
    void openEditorRequested(PluginInstance* plugin);
    void scanRequested();

private slots:
    void onAddClicked();
    void onRemoveClicked(int index);
    void onEditorClicked(int index);
    void onMoveUpClicked(int index);
    void onMoveDownClicked(int index);

private:
    void buildRow(PluginInstance* plugin, int index);
    PluginChain* targetChain() const;

    Track* m_track = nullptr;
    AudioBus* m_bus = nullptr;
    PluginManager* m_pluginManager = nullptr;

    QVBoxLayout* m_mainLayout = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_container = nullptr;
    QVBoxLayout* m_containerLayout = nullptr;
    QPushButton* m_addButton = nullptr;

    std::vector<QWidget*> m_rows;
};
