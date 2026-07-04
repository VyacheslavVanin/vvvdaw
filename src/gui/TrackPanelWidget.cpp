#include "TrackPanelWidget.h"
#include "model/Track.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenu>
#include <QContextMenuEvent>

TrackPanelWidget::TrackPanelWidget(Track* track, QWidget* parent)
    : QWidget(parent)
    , m_track(track)
{
    setFixedWidth(200);
    setMinimumHeight(60);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    auto* topRow = new QHBoxLayout;
    m_nameLabel = new QLabel(track ? track->name() : "Track", this);
    m_nameLabel->setStyleSheet("font-weight: bold; font-size: 11px;");
    topRow->addWidget(m_nameLabel, 1);

    auto makeBtn = [&](const QString& text, const QString& style) {
        auto* btn = new QPushButton(text, this);
        btn->setFixedSize(22, 22);
        btn->setCheckable(true);
        btn->setStyleSheet(style);
        topRow->addWidget(btn);
        return btn;
    };

    m_armButton = makeBtn("R",
        "QPushButton { background: #442222; color: #cc6666; border: 1px solid #664444; font-weight: bold; font-size: 9px; }"
        "QPushButton:checked { background: #cc2222; color: white; border: 2px solid #ff4444; }");
    m_armButton->setToolTip("Record Arm");

    m_soloButton = makeBtn("S",
        "QPushButton { background: #443322; color: #ccaa66; border: 1px solid #665544; font-weight: bold; font-size: 9px; }"
        "QPushButton:checked { background: #cc8800; color: white; border: 2px solid #ffaa00; }");
    m_soloButton->setToolTip("Solo");

    m_muteButton = makeBtn("M",
        "QPushButton { background: #334433; color: #66cc66; border: 1px solid #446644; font-weight: bold; font-size: 9px; }"
        "QPushButton:checked { background: #33aa33; color: white; border: 2px solid #44ff44; }");
    m_muteButton->setToolTip("Mute");

    m_monitorButton = makeBtn("MON",
        "QPushButton { background: #223344; color: #6688cc; border: 1px solid #445566; font-weight: bold; font-size: 8px; }"
        "QPushButton:checked { background: #2244aa; color: white; border: 2px solid #4488ff; }");
    m_monitorButton->setToolTip("Input Monitoring");

    layout->addLayout(topRow);

    auto* panRow = new QHBoxLayout;
    auto* panLabel = new QLabel("pan:", this);
    panLabel->setStyleSheet("font-size: 10px; color: #999;");
    panRow->addWidget(panLabel);
    m_panSlider = new QSlider(Qt::Horizontal, this);
    m_panSlider->setRange(-100, 100);
    m_panSlider->setValue(0);
    m_panSlider->setFixedHeight(14);
    panRow->addWidget(m_panSlider, 1);
    layout->addLayout(panRow);

    auto* volRow = new QHBoxLayout;
    auto* volLabel = new QLabel("level:", this);
    volLabel->setStyleSheet("font-size: 10px; color: #999;");
    volRow->addWidget(volLabel);
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedHeight(14);
    volRow->addWidget(m_volumeSlider, 1);
    layout->addLayout(volRow);

    connect(m_armButton, &QPushButton::toggled, this, [this](bool checked) {
        if (m_track) m_track->setRecordArmed(checked);
        emit armToggled(checked);
    });
    connect(m_soloButton, &QPushButton::toggled, this, [this](bool checked) {
        if (m_track) m_track->setSolo(checked);
        emit soloToggled(checked);
    });
    connect(m_muteButton, &QPushButton::toggled, this, [this](bool checked) {
        if (m_track) m_track->setMuted(checked);
        emit muteToggled(checked);
    });
    connect(m_monitorButton, &QPushButton::toggled, this, [this](bool checked) {
        if (m_track) m_track->setMonitoring(checked);
        emit monitorToggled(checked);
    });
    connect(m_panSlider, &QSlider::valueChanged, this, [this](int val) {
        float pan = val / 100.0f;
        if (m_track) m_track->setPan(pan);
        emit panChanged(pan);
    });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        float vol = val / 100.0f;
        if (m_track) m_track->setVolume(vol);
        emit volumeChanged(vol);
    });
}

void TrackPanelWidget::updateFromTrack() {
    if (!m_track) return;
    m_nameLabel->setText(m_track->name());
    m_armButton->setChecked(m_track->isRecordArmed());
    m_soloButton->setChecked(m_track->isSolo());
    m_muteButton->setChecked(m_track->isMuted());
    m_monitorButton->setChecked(m_track->isMonitoring());
    m_panSlider->setValue(static_cast<int>(m_track->pan() * 100));
    m_volumeSlider->setValue(static_cast<int>(m_track->volume() * 100));
}

void TrackPanelWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QAction* deleteAction = menu.addAction("Delete Track");
    connect(deleteAction, &QAction::triggered, this, [this] {
        QMetaObject::invokeMethod(this, [this] { emit deleteRequested(); }, Qt::QueuedConnection);
    });
    menu.exec(event->globalPos());
}
