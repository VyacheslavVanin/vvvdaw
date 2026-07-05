#pragma once
#include <QWidget>

class QPushButton;
class QLabel;

class TransportPanel : public QWidget {
    Q_OBJECT
public:
    explicit TransportPanel(QWidget* parent = nullptr);

    void setTimeText(const QString& text);
    void setPlaying(bool playing);
    void setRecording(bool recording);
    void setSnapToGrid(bool snap);

signals:
    void backClicked();
    void playClicked();
    void pauseClicked();
    void stopClicked();
    void recordClicked();
    void forwardClicked();
    void snapToggled(bool snap);

private:
    QPushButton* m_backButton;
    QPushButton* m_playButton;
    QPushButton* m_pauseButton;
    QPushButton* m_stopButton;
    QPushButton* m_recordButton;
    QPushButton* m_forwardButton;
    QPushButton* m_snapButton;
    QLabel* m_timeLabel;
    bool m_snapToGrid = true;
};
