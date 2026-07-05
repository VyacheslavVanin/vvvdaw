#pragma once
#include <QWidget>

class QDoubleSpinBox;
class QComboBox;

class TempoWidget : public QWidget {
    Q_OBJECT
public:
    explicit TempoWidget(QWidget* parent = nullptr);

    void setTempo(double bpm);
    void setTimeSignature(int num, int den);
    void setSnapResolution(int index); // index into s_resolutions

signals:
    void tempoChanged(double bpm);
    void timeSignatureChanged(int num, int den);
    void snapResolutionChanged(double snapUnit);

private:
    QDoubleSpinBox* m_bpmSpin;
    QComboBox* m_sigCombo;
    QComboBox* m_snapCombo;
};
