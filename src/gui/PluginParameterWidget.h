#pragma once
#include <QWidget>
#include <vector>

class PluginInstance;
struct PluginPortInfo;
class RotaryKnob;
class QLabel;
class QLineEdit;

class PluginParameterWidget : public QWidget {
    Q_OBJECT
public:
    explicit PluginParameterWidget(PluginInstance* plugin,
                                   int knobsPerRow = 3,
                                   QWidget* parent = nullptr);
    ~PluginParameterWidget();

signals:
    void parameterChangeRequested(int paramIndex, float oldValue, float newValue);
    void pathParameterChangeRequested(int paramIndex, const QString& oldValue, const QString& newValue);

private:
    PluginInstance* m_plugin = nullptr;
    std::vector<int> m_paramIndices;
    std::vector<RotaryKnob*> m_knobs;

    struct KnobRange { float min; float range; QLabel* label = nullptr; };
    std::vector<KnobRange> m_knobRanges;

    struct StringParamInfo { int paramIndex; QLineEdit* edit = nullptr; };
    std::vector<StringParamInfo> m_stringParams;
};
