#include "TrackPanelWidget.h"
#include "model/Track.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenu>
#include <QContextMenuEvent>
#include <QMouseEvent>

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
    m_nameEdit = new QLineEdit(track ? track->name() : "Track", this);
    m_nameEdit->setReadOnly(true);
    m_nameEdit->setStyleSheet(
        "QLineEdit { background: transparent; border: none; font-weight: bold; font-size: 11px; color: #ccc; }"
        "QLineEdit:focus { background: #333; border: 1px solid #6688cc; }"
    );
    m_nameEdit->installEventFilter(this);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this] {
        if (!m_track) return;
        QString text = m_nameEdit->text().trimmed();
        if (text.isEmpty()) {
            m_nameEdit->setText(m_track->name());
        } else {
            m_track->setName(text);
        }
        m_nameEdit->setReadOnly(true);
        m_nameEdit->setSelection(0, 0);
    });
    topRow->addWidget(m_nameEdit, 1);

    auto makeBtn = [&](const QString& text, const QString& style) {
        auto* btn = new QPushButton(text, this);
        btn->setFixedSize(22, 22);
        btn->setCheckable(true);
        btn->setStyleSheet(style);
        topRow->addWidget(btn);
        return btn;
    };

    auto btnStyle = [](const QString& normal, const QString& checked) {
        return normal + "; padding: 0px; }"
             + checked + "; padding: 0px; }";
    };

    m_armButton = makeBtn(QString::fromUtf8("\xe2\x97\x8f"),
        btnStyle(
            "QPushButton { background: #442222; color: #cc6666; border: 1px solid #664444; font-weight: bold; font-size: 12px",
            "QPushButton:checked { background: #cc2222; color: white; border: 2px solid #ff4444; font-weight: bold; font-size: 12px"
        ));
    m_armButton->setToolTip("Record Arm");

    m_soloButton = makeBtn("S",
        btnStyle(
            "QPushButton { background: #443322; color: #ccaa66; border: 1px solid #665544; font-weight: bold; font-size: 10px",
            "QPushButton:checked { background: #cc8800; color: white; border: 2px solid #ffaa00; font-weight: bold; font-size: 10px"
        ));
    m_soloButton->setToolTip("Solo");

    m_muteButton = makeBtn("M",
        btnStyle(
            "QPushButton { background: #334433; color: #66cc66; border: 1px solid #446644; font-weight: bold; font-size: 10px",
            "QPushButton:checked { background: #33aa33; color: white; border: 2px solid #44ff44; font-weight: bold; font-size: 10px"
        ));
    m_muteButton->setToolTip("Mute");

    m_monitorButton = makeBtn("MON",
        btnStyle(
            "QPushButton { background: #223344; color: #6688cc; border: 1px solid #445566; font-weight: bold; font-size: 9px",
            "QPushButton:checked { background: #2244aa; color: white; border: 2px solid #4488ff; font-weight: bold; font-size: 9px"
        ));
    m_monitorButton->setToolTip("Input Monitoring");

    layout->addLayout(topRow);

    auto* panRow = new QHBoxLayout;
    auto* panLabel = new QLabel("pan:", this);
    panLabel->setStyleSheet("font-size: 10px; color: #aaa;");
    panRow->addWidget(panLabel);
    m_panSlider = new QSlider(Qt::Horizontal, this);
    m_panSlider->setRange(-100, 100);
    m_panSlider->setValue(0);
    m_panSlider->setFixedHeight(14);
    m_panSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #444; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #aaa; width: 10px; margin: -4px 0; border-radius: 5px; }"
        "QSlider::sub-page:horizontal { background: #6688cc; border-radius: 2px; }"
    );
    panRow->addWidget(m_panSlider, 1);
    layout->addLayout(panRow);

    auto* volRow = new QHBoxLayout;
    auto* volLabel = new QLabel("level:", this);
    volLabel->setStyleSheet("font-size: 10px; color: #aaa;");
    volRow->addWidget(volLabel);
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedHeight(14);
    m_volumeSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #444; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #aaa; width: 10px; margin: -4px 0; border-radius: 5px; }"
        "QSlider::sub-page:horizontal { background: #44aa44; border-radius: 2px; }"
    );
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
    // Capture undo snapshot before user interaction starts
    connect(m_panSlider, &QSlider::sliderPressed, this, [this] { emit beforeModify(); });
    connect(m_volumeSlider, &QSlider::sliderPressed, this, [this] { emit beforeModify(); });
    for (auto* btn : {m_muteButton, m_soloButton, m_armButton, m_monitorButton}) {
        connect(btn, &QPushButton::pressed, this, [this] { emit beforeModify(); });
    }

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
    m_nameEdit->setText(m_track->name());
    m_armButton->setChecked(m_track->isRecordArmed());
    m_soloButton->setChecked(m_track->isSolo());
    m_muteButton->setChecked(m_track->isMuted());
    m_monitorButton->setChecked(m_track->isMonitoring());
    m_panSlider->setValue(static_cast<int>(m_track->pan() * 100));
    m_volumeSlider->setValue(static_cast<int>(m_track->volume() * 100));
}

void TrackPanelWidget::setAlternateRow(bool alternate) {
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, alternate ? QColor("#2f2f2f") : QColor("#2a2a2a"));
    setPalette(p);
}

bool TrackPanelWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_nameEdit && event->type() == QEvent::MouseButtonDblClick) {
        m_nameEdit->setReadOnly(false);
        m_nameEdit->selectAll();
        m_nameEdit->setFocus();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void TrackPanelWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QAction* addAction = menu.addAction("Add Track");
    connect(addAction, &QAction::triggered, this, [this] {
        QMetaObject::invokeMethod(this, [this] { emit addTrackRequested(); }, Qt::QueuedConnection);
    });
    menu.addSeparator();
    QAction* deleteAction = menu.addAction("Delete Track");
    connect(deleteAction, &QAction::triggered, this, [this] {
        QMetaObject::invokeMethod(this, [this] { emit deleteRequested(); }, Qt::QueuedConnection);
    });
    menu.exec(event->globalPos());
}
