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

signals:
    void backClicked();
    void playClicked();
    void pauseClicked();
    void stopClicked();
    void recordClicked();
    void forwardClicked();

private:
    QPushButton* m_backButton;
    QPushButton* m_playButton;
    QPushButton* m_pauseButton;
    QPushButton* m_stopButton;
    QPushButton* m_recordButton;
    QPushButton* m_forwardButton;
    QLabel* m_timeLabel;
};
