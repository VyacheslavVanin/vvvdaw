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
    explicit PluginWindow(PluginInstance* plugin,
                          int knobsPerRow = 3,
                          QWidget* parent = nullptr);
    ~PluginWindow() override;

    void open();
    void close();
    PluginInstance* plugin() const { return m_plugin; }

signals:
    void windowClosed();
    void parameterChangeRequested(int paramIndex, float oldValue, float newValue);
    void pathParameterChangeRequested(int paramIndex, const QString& oldValue, const QString& newValue);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    PluginInstance* m_plugin = nullptr;
    void* m_editorHandle = nullptr;
    QWindow* m_editorWindow = nullptr;
    int m_knobsPerRow = 3;
};
