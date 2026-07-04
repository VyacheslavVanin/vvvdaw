#include "TransportPanel.h"
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

TransportPanel::TransportPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);

    auto makeBtn = [&](const QString& text, const QString& tooltip) {
        auto* btn = new QPushButton(text, this);
        btn->setFixedHeight(28);
        btn->setToolTip(tooltip);
        layout->addWidget(btn);
        return btn;
    };

    m_backButton = makeBtn("⏮", "Back to start");
    m_playButton = makeBtn("▶", "Play");
    m_pauseButton = makeBtn("⏸", "Pause");
    m_stopButton = makeBtn("⏹", "Stop");
    m_recordButton = makeBtn("●", "Record");
    m_forwardButton = makeBtn("⏭", "Forward to end");

    connect(m_backButton, &QPushButton::clicked, this, &TransportPanel::backClicked);
    connect(m_playButton, &QPushButton::clicked, this, &TransportPanel::playClicked);
    connect(m_pauseButton, &QPushButton::clicked, this, &TransportPanel::pauseClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &TransportPanel::stopClicked);
    connect(m_recordButton, &QPushButton::clicked, this, &TransportPanel::recordClicked);
    connect(m_forwardButton, &QPushButton::clicked, this, &TransportPanel::forwardClicked);

    m_timeLabel = new QLabel("00:00:00.000", this);
    m_timeLabel->setStyleSheet("padding: 0 8px;");
    QFont monoFont("monospace");
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPixelSize(14);
    m_timeLabel->setFont(monoFont);
    QFontMetrics fm(monoFont);
    m_timeLabel->setFixedWidth(fm.horizontalAdvance("999:59:59.999") + 18);
    layout->addWidget(m_timeLabel);

    layout->addStretch();
    setFixedHeight(40);
}

void TransportPanel::setTimeText(const QString& text) {
    m_timeLabel->setText(text);
}

void TransportPanel::setPlaying(bool playing) {
    m_playButton->setStyleSheet(playing
        ? "QPushButton { background: #226622; color: white; border: 1px solid #44aa44; }"
        : "");
}

void TransportPanel::setRecording(bool recording) {
    m_recordButton->setStyleSheet(recording
        ? "QPushButton { background: #882222; color: white; border: 1px solid #cc4444; }"
        : "");
}
