#include "PianoRollWindow.h"
#include "PianoRollWidget.h"
#include "VelocityEditorWidget.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/MidiEvent.h"
#include "core/UndoStack.h"
#include "audio/AudioEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QCloseEvent>
#include <QShortcut>
#include <QWheelEvent>

namespace {
// Forwards wheel events that hit the (non-vertically-scrollable) velocity
// lane to the note grid's scroll area so the whole piano roll scrolls.
class WheelForwardFilter : public QObject {
public:
    WheelForwardFilter(QObject* target, QObject* parent)
        : QObject(parent), m_target(target) {}
protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::Wheel && !ev->isAccepted()) {
            QCoreApplication::sendEvent(m_target, ev);
            return true;
        }
        return QObject::eventFilter(obj, ev);
    }
private:
    QObject* m_target;
};
} // namespace

PianoRollWindow::PianoRollWindow(Project& project, UndoStack& undo, AudioEngine& engine,
                                 int trackIndex, int64_t eventId, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::WindowStaysOnTopHint)
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

    // S toggles snap-to-grid (routed through the main window to keep all UIs in sync).
    auto* snapShortcut = new QShortcut(QKeySequence(Qt::Key_S), this);
    connect(snapShortcut, &QShortcut::activated, this,
            [this] { emit toggleSnapRequested(); });

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

    auto* velLabel = new QLabel("Velocity:", this);
    velLabel->setStyleSheet("color: #aaa; font-size: 11px;");
    toolbar->addWidget(velLabel);
    auto* velSpin = new QSpinBox(this);
    velSpin->setRange(1, 127);
    velSpin->setValue(m_widget ? m_widget->defaultVelocity() : 100);
    velSpin->setStyleSheet(
        "QSpinBox { background: #333; color: #ccc; border: 1px solid #555; font-size: 11px; padding: 1px 4px; }"
        "QSpinBox::up-button, QSpinBox::down-button { width: 14px; background: #444; border: none; }"
    );
    toolbar->addWidget(velSpin);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    connect(velSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
        if (m_widget) m_widget->setDefaultVelocity(v);
    });

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_widget = new PianoRollWidget(m_project, undo, m_trackIndex, m_eventId, scrollArea);
    scrollArea->setWidget(m_widget);
    layout->addWidget(scrollArea, 1);

    // Always-visible velocity editor, horizontally synced with the grid.
    auto* velScroll = new QScrollArea(this);
    velScroll->setWidgetResizable(true);
    velScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    velScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_velocityEditor = new VelocityEditorWidget(m_project, undo, m_trackIndex, m_eventId, velScroll);
    velScroll->setWidget(m_velocityEditor);
    layout->addWidget(velScroll);

    // Scroll the grid when the wheel is used over the velocity lane.
    velScroll->viewport()->installEventFilter(
        new WheelForwardFilter(scrollArea->viewport(), this));

    connect(m_widget, &PianoRollWidget::playheadSetRequested, this, [this](int64_t sample) {
        m_engine.setPlayPosition(sample);
        m_widget->setPlayheadSample(sample);
    });

    connect(m_widget, &PianoRollWidget::notePreviewOn, this,
            [this](int pitch, int velocity) {
        m_engine.previewNoteOn(m_trackIndex, pitch, velocity);
    });
    connect(m_widget, &PianoRollWidget::notePreviewOff, this,
            [this](int pitch) {
        m_engine.previewNoteOff(m_trackIndex, pitch);
    });

    // Keep the velocity editor in lockstep with the note grid.
    connect(m_widget, &PianoRollWidget::notesChanged, this,
            [this] { m_velocityEditor->reload(); });
    connect(m_widget, &PianoRollWidget::selectionChanged, this,
            [this] { m_velocityEditor->setSelection(m_widget->selectedNotes()); });
    connect(m_widget, &PianoRollWidget::zoomChanged, this,
            [this](double p) { m_velocityEditor->setPixelsPerTick(p); });

    auto syncScroll = [this](QScrollBar* target, int value) {
        if (m_syncingScroll) return;
        m_syncingScroll = true;
        target->setValue(value);
        m_syncingScroll = false;
    };
    connect(scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [syncScroll, velScroll](int v) {
        syncScroll(velScroll->horizontalScrollBar(), v);
    });
    connect(velScroll->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [syncScroll, scrollArea](int v) {
        syncScroll(scrollArea->horizontalScrollBar(), v);
    });

    connect(snapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, snapCombo](int) {
        m_widget->setSnapDiv(snapCombo->currentData().toInt());
    });

    resize(900, 720);
}

PianoRollWindow::~PianoRollWindow() {
    // Release any preview note still held when the window is destroyed (e.g.
    // app shutdown) so no note rings forever.
    m_engine.cancelPreviewNotes(m_trackIndex);
}

bool PianoRollWindow::reload() {
    bool ok = m_widget && m_widget->reload();
    if (m_velocityEditor)
        m_velocityEditor->reload();
    return ok;
}

void PianoRollWindow::setPlayheadSample(int64_t sample) {
    if (m_widget)
        m_widget->setPlayheadSample(sample);
}

void PianoRollWindow::closeEvent(QCloseEvent* event) {
    m_engine.cancelPreviewNotes(m_trackIndex);
    emit windowClosed();
    QWidget::closeEvent(event);
}
