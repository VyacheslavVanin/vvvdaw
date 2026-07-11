#pragma once
#include <QWidget>
#include <vector>

class PluginInstance;
struct PluginPortInfo;

class PluginParameterWidget : public QWidget {
    Q_OBJECT
public:
    explicit PluginParameterWidget(PluginInstance* plugin, QWidget* parent = nullptr);

private slots:
    void onSliderChanged(int paramIndex, float normalizedValue);

private:
    PluginInstance* m_plugin = nullptr;
    std::vector<int> m_paramIndices;
};
