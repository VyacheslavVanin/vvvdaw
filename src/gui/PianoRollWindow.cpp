#include "PianoRollWindow.h"
#include "PianoRollWidget.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/MidiEvent.h"
#include "core/UndoStack.h"
#include "audio/AudioEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QScrollArea>
#include <QCloseEvent>
#include <QShortcut>

PianoRollWindow::PianoRollWindow(Project& project, UndoStack& undo, AudioEngine& engine,
                                 int trackIndex, int64_t eventId, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_project(project)
    , m_engine(engine)
    , m_trackIndex(trackIndex)
    , m_eventId(eventId)
{
    setWindowTitle("Piano Roll");

    // Space toggles play/pause (window-scoped, so it works with any child focused).
    auto* playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(playShortcut, &QShortcut::activated, this, [this] {
        vvvdaw::TransportState s = m_engine.transportState();
        if (s == vvvdaw::TransportState::Playing || s == vvvdaw::TransportState::Recording)
            m_engine.setTransportState(vvvdaw::TransportState::Paused);
        else
            m_engine.setTransportState(vvvdaw::TransportState::Playing);
    });

    // Undo/redo request the main window to perform them (it also refreshes UI).
    auto* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, [this] { emit undoRequested(); });
    auto* redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(redoShortcut, &QShortcut::activated, this, [this] { emit redoRequested(); });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(6, 4, 6, 4);
    auto* snapLabel = new QLabel("Note Length:", this);
    snapLabel->setStyleSheet("color: #aaa; font-size: 11px;");
    toolbar->addWidget(snapLabel);
    auto* snapCombo = new QComboBox(this);
    snapCombo->addItem("1/4", 1);
    snapCombo->addItem("1/8", 2);
    snapCombo->addItem("1/16", 4);
    snapCombo->addItem("1/32", 8);
    snapCombo->setCurrentIndex(2);
    snapCombo->setStyleSheet(
        "QComboBox { background: #333; color: #ccc; border: 1px solid #555; font-size: 11px; padding: 1px 4px; }"
        "QComboBox::drop-down { border: none; width: 14px; }"
        "QComboBox QAbstractItemView { background: #333; color: #ccc; selection-background-color: #094771; }"
    );
    toolbar->addWidget(snapCombo);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_widget = new PianoRollWidget(m_project, undo, m_trackIndex, m_eventId, scrollArea);
    scrollArea->setWidget(m_widget);
    layout->addWidget(scrollArea, 1);

    connect(m_widget, &PianoRollWidget::playheadSetRequested, this, [this](int64_t sample) {
        m_engine.setPlayPosition(sample);
        m_widget->setPlayheadSample(sample);
    });

    connect(snapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, snapCombo](int) {
        m_widget->setSnapDiv(snapCombo->currentData().toInt());
    });

    resize(900, 720);
}

PianoRollWindow::~PianoRollWindow() = default;

bool PianoRollWindow::reload() {
    return m_widget && m_widget->reload();
}

void PianoRollWindow::setPlayheadSample(int64_t sample) {
    if (m_widget)
        m_widget->setPlayheadSample(sample);
}

void PianoRollWindow::closeEvent(QCloseEvent* event) {
    emit windowClosed();
    QWidget::closeEvent(event);
}
