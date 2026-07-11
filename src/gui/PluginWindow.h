#pragma once
#include <QWidget>
#include <QWindow>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>

class PluginInstance;

class PluginWindow : public QWidget {
    Q_OBJECT
public:
    explicit PluginWindow(PluginInstance* plugin, QWidget* parent = nullptr);
    ~PluginWindow() override;

    void open();
    void close();

signals:
    void windowClosed();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    PluginInstance* m_plugin = nullptr;
    void* m_editorHandle = nullptr;
    QWindow* m_editorWindow = nullptr;
};
