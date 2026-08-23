#include <QTest>
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QPixmap>
#include <QSignalSpy>
#include <QTimer>
#include <QContextMenuEvent>
#include <QMimeData>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDragLeaveEvent>
#include <QFrame>
#include <QDialog>
#include <QWheelEvent>
#include <QScrollBar>
#include <algorithm>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <portaudio.h>

#include "core/Settings.h"
#include "audio/AudioEngine.h"
#include "audio/AudioUtils.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioClip.h"
#include "model/AudioBus.h"
#include "model/Instrument.h"
#include "model/TemplateStore.h"
#include "plugin/PluginInstance.h"
#include "plugin/PluginManager.h"
#include "gui/MainWindow.h"
#include "gui/StartDialog.h"
#include "gui/TrackPanelWidget.h"
#include "gui/PanSlider.h"
#include "gui/TrackViewWidget.h"
#include "gui/WaveformPainter.h"
#include "gui/TimelineRuler.h"
#include "gui/MeasureRuler.h"
#include "gui/BusPanelWidget.h"
#include "gui/BusSendsWidget.h"
#include "gui/BusLevelMeter.h"
#include "gui/BusColorBar.h"
#include "gui/InstrumentPanelWidget.h"
#include "gui/PluginListWidget.h"
#include "gui/PluginWindow.h"
#include "gui/PianoRollWindow.h"
#include "gui/PianoRollWidget.h"
#include "gui/ChannelRoutingDialog.h"
#include "gui/SettingsDialog.h"
#include "commands/TrackCommands.h"
#include "commands/SnapshotCommand.h"
#include "GuiTestHelpers.h"

class PluginListTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void pluginListRowsStayCompact();
    void pluginListHasNoRemoveButton();
    void pluginListContextMenuRemovesPlugin();
    void pluginListContextMenuEmptySpaceOffersAddPlugin();
    void pluginListContextMenuRowOffersAddAndRemove();
    void pluginListContextMenuAddOpensPicker();
    void pluginListDragShowsInsertionLine();
    void pluginListDropFollowsInsertionLine();
    void pluginWindowStaysOnTop();
    void pianoRollWindowStaysOnTop();
private:
    GuiTestEnv m_env;
};

void PluginListTest::initTestCase() {
    if (!m_env.init())
        QSKIP("PortAudio not available");
}

void PluginListTest::cleanupTestCase() {
    m_env.cleanup();
}

void PluginListTest::pluginListRowsStayCompact() {
    AudioBus bus;
    for (int i = 0; i < 3; ++i) {
        auto synth = std::make_unique<StubSynth>();
        bus.pluginChain().addPlugin(std::move(synth));
    }

    PluginListWidget list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 200);
    list.show();
    QCoreApplication::processEvents();

    // Each plugin row keeps a compact height instead of stretching to fill
    // the whole list (which would give ~66 px per row here).
    int rowCount = 0;
    for (QPushButton* b : list.findChildren<QPushButton*>()) {
        if (b->text() == "ON" || b->text() == "OFF") {
            ++rowCount;
            QVERIFY(b->parentWidget()->height() <= 30);
        }
    }
    QCOMPARE(rowCount, 3);
}


void PluginListTest::pluginListHasNoRemoveButton() {
    AudioBus bus;
    auto synth = std::make_unique<StubSynth>();
    bus.pluginChain().addPlugin(std::move(synth));

    PluginListWidget list;
    list.setBus(&bus);
    list.rebuild();

    for (QPushButton* b : list.findChildren<QPushButton*>())
        QVERIFY(b->text() != "x"); // deletion is done via the context menu
}


void PluginListTest::pluginListContextMenuRemovesPlugin() {
    AudioBus bus;
    auto synth = std::make_unique<StubSynth>();
    bus.pluginChain().addPlugin(std::move(synth));

    PluginListWidget list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 120);
    list.show();
    QCoreApplication::processEvents();

    QPushButton* enableBtn = nullptr;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON") { enableBtn = b; break; }
    QVERIFY(enableBtn);
    QWidget* row = enableBtn->parentWidget();
    QCOMPARE(bus.pluginChain().count(), 1);

    // Drive the context menu: open it on the row and trigger "Remove Plugin".
    QTimer::singleShot(0, [] {
        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMenu*>(w)) { menu = m; break; }
        if (!menu) return;
        for (QAction* a : menu->actions()) {
            if (a->text().contains("Remove")) {
                a->trigger();
                break;
            }
        }
        menu->close();
    });

    QContextMenuEvent ev(QContextMenuEvent::Mouse, row->rect().center(),
                         row->mapToGlobal(row->rect().center()));
    QApplication::sendEvent(row, &ev);
    QCoreApplication::processEvents();

    QCOMPARE(bus.pluginChain().count(), 0);
}


void PluginListTest::pluginListContextMenuEmptySpaceOffersAddPlugin() {
    AudioBus bus;
    bus.pluginChain().addPlugin(std::make_unique<StubSynth>());

    PluginListWidget list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 200);
    list.show();
    QCoreApplication::processEvents();

    QPushButton* enableBtn = nullptr;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON") { enableBtn = b; break; }
    QVERIFY(enableBtn);
    QWidget* row = enableBtn->parentWidget();
    QWidget* container = row->parentWidget();
    QVERIFY(container);

    bool hasAdd = false;
    bool hasRemove = false;
    QTimer::singleShot(0, [&hasAdd, &hasRemove] {
        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMenu*>(w)) { menu = m; break; }
        if (!menu) return;
        for (QAction* a : menu->actions()) {
            if (a->text().contains("Add Plugin")) hasAdd = true;
            if (a->text().contains("Remove")) hasRemove = true;
        }
        menu->close();
    });

    // Right-click empty space below the last row.
    QPoint emptyPos(container->width() / 2, row->geometry().bottom() + 20);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, emptyPos,
                         container->mapToGlobal(emptyPos));
    QApplication::sendEvent(container, &ev);
    QCoreApplication::processEvents();

    QVERIFY(hasAdd);
    QVERIFY(!hasRemove); // no row under the cursor
}


void PluginListTest::pluginListContextMenuRowOffersAddAndRemove() {
    AudioBus bus;
    bus.pluginChain().addPlugin(std::make_unique<StubSynth>());

    PluginListWidget list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 120);
    list.show();
    QCoreApplication::processEvents();

    QPushButton* enableBtn = nullptr;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON") { enableBtn = b; break; }
    QVERIFY(enableBtn);
    QWidget* row = enableBtn->parentWidget();

    bool hasAdd = false;
    bool hasRemove = false;
    QTimer::singleShot(0, [&hasAdd, &hasRemove] {
        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMenu*>(w)) { menu = m; break; }
        if (!menu) return;
        for (QAction* a : menu->actions()) {
            if (a->text().contains("Add Plugin")) hasAdd = true;
            if (a->text().contains("Remove")) hasRemove = true;
        }
        menu->close();
    });

    QContextMenuEvent ev(QContextMenuEvent::Mouse, row->rect().center(),
                         row->mapToGlobal(row->rect().center()));
    QApplication::sendEvent(row, &ev);
    QCoreApplication::processEvents();

    QVERIFY(hasAdd);
    QVERIFY(hasRemove);
}


void PluginListTest::pluginListContextMenuAddOpensPicker() {
    AudioBus bus;
    bus.pluginChain().addPlugin(std::make_unique<StubSynth>());

    PluginManager manager;
    PluginListWidget list;
    list.setBus(&bus);
    list.setPluginManager(&manager);
    list.rebuild();
    list.resize(240, 200);
    list.show();
    QCoreApplication::processEvents();

    QPushButton* enableBtn = nullptr;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON") { enableBtn = b; break; }
    QVERIFY(enableBtn);
    QWidget* row = enableBtn->parentWidget();
    QWidget* container = row->parentWidget();
    QVERIFY(container);

    bool dialogSeen = false;
    QTimer::singleShot(0, [&dialogSeen] {
        // The "Add Plugin..." action opens a modal picker dialog; the second
        // timer runs inside that dialog's event loop.
        QTimer::singleShot(0, [&dialogSeen] {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* dlg = qobject_cast<QDialog*>(w)) {
                    if (dlg->findChild<QListWidget*>()) {
                        dialogSeen = true;
                        dlg->reject();
                        return;
                    }
                }
            }
        });
        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMenu*>(w)) { menu = m; break; }
        if (!menu) return;
        for (QAction* a : menu->actions())
            if (a->text().contains("Add Plugin")) { a->trigger(); break; }
        menu->close();
    });

    QPoint emptyPos(container->width() / 2, row->geometry().bottom() + 20);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, emptyPos,
                         container->mapToGlobal(emptyPos));
    QApplication::sendEvent(container, &ev);
    QCoreApplication::processEvents();

    QVERIFY(dialogSeen);
}


void PluginListTest::pluginListDragShowsInsertionLine() {
    static const char* const kMimePluginIndex = "application/x-vvvdaw-plugin-index";

    // Exposes the protected drag handlers so the widget's own drag logic can
    // be exercised directly. Qt's global QDragManager intercepts synthetic
    // drag events sent via QApplication::sendEvent, so we call the handlers
    // the way the drag state machine would.
    class TestablePluginList : public PluginListWidget {
    public:
        using PluginListWidget::dragMoveEvent;
        using PluginListWidget::dragLeaveEvent;
        using PluginListWidget::dropEvent;
    };

    AudioBus bus;
    for (int i = 0; i < 3; ++i)
        bus.pluginChain().addPlugin(std::make_unique<StubSynth>());

    TestablePluginList list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 200);
    list.show();
    QCoreApplication::processEvents();

    // Locate the rendered rows (the widget holding each ON/OFF button).
    std::vector<QWidget*> rows;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON" || b->text() == "OFF")
            rows.push_back(b->parentWidget());
    QCOMPARE(static_cast<int>(rows.size()), 3);

    auto* line = list.findChild<QFrame*>("pluginInsertionLine");
    QVERIFY(line);
    QVERIFY(!line->isVisible());

    // Drag plugin 0 over the upper half of the third row: the line must sit at
    // the boundary above that row (container coordinates), full width.
    QMimeData mime;
    mime.setData(kMimePluginIndex, QByteArray::number(0));
    QPoint listPos = rows[2]->mapTo(&list, QPoint(0, 2));
    QDragMoveEvent moveEv(listPos, Qt::MoveAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    list.dragMoveEvent(&moveEv);

    QVERIFY(line->isVisible());
    QCOMPARE(line->x(), 0);
    QCOMPARE(line->height(), 2);
    QCOMPARE(line->y(), rows[2]->pos().y() - 1);
    QVERIFY(line->width() >= rows[0]->width());

    // Leaving the list hides the line again.
    QDragLeaveEvent leaveEv;
    list.dragLeaveEvent(&leaveEv);
    QVERIFY(!line->isVisible());
}


void PluginListTest::pluginListDropFollowsInsertionLine() {
    static const char* const kMimePluginIndex = "application/x-vvvdaw-plugin-index";

    class TestablePluginList : public PluginListWidget {
    public:
        using PluginListWidget::dropEvent;
    };

    AudioBus bus;
    PluginInstance* p0 = nullptr;
    PluginInstance* p1 = nullptr;
    PluginInstance* p2 = nullptr;
    for (int i = 0; i < 3; ++i) {
        auto synth = std::make_unique<StubSynth>();
        if (i == 0) p0 = synth.get();
        if (i == 1) p1 = synth.get();
        if (i == 2) p2 = synth.get();
        bus.pluginChain().addPlugin(std::move(synth));
    }

    TestablePluginList list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 200);
    list.show();
    QCoreApplication::processEvents();

    std::vector<QWidget*> rows;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON" || b->text() == "OFF")
            rows.push_back(b->parentWidget());
    QCOMPARE(static_cast<int>(rows.size()), 3);

    // Drop plugin 0 at the upper half of the third row: the boundary is before
    // the third row, so the order becomes p1, p0, p2.
    QMimeData mime;
    mime.setData(kMimePluginIndex, QByteArray::number(0));
    QPoint listPos = rows[2]->mapTo(&list, QPoint(0, 2));
    QDropEvent dropEv(QPointF(listPos), Qt::MoveAction, &mime,
                      Qt::LeftButton, Qt::NoModifier);
    list.dropEvent(&dropEv);

    QCOMPARE(bus.pluginChain().plugin(0), p1);
    QCOMPARE(bus.pluginChain().plugin(1), p0);
    QCOMPARE(bus.pluginChain().plugin(2), p2);

    // Dropping below the last row appends the dragged plugin at the end.
    list.rebuild();
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    rows.clear();
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON" || b->text() == "OFF")
            rows.push_back(b->parentWidget());
    QCOMPARE(static_cast<int>(rows.size()), 3);

    QMimeData mime2;
    mime2.setData(kMimePluginIndex, QByteArray::number(0)); // drag p1 (now at index 0)
    QPoint below = rows[2]->mapTo(&list, QPoint(0, rows[2]->height()));
    QDropEvent dropBelow(QPointF(below), Qt::MoveAction, &mime2,
                         Qt::LeftButton, Qt::NoModifier);
    list.dropEvent(&dropBelow);

    // Order was [p1, p0, p2]; dragging p1 past the end appends it.
    QCOMPARE(bus.pluginChain().plugin(0), p0);
    QCOMPARE(bus.pluginChain().plugin(1), p2);
    QCOMPARE(bus.pluginChain().plugin(2), p1);
}


void PluginListTest::pluginWindowStaysOnTop() {
    auto plugin = std::make_unique<StubSynth>();
    PluginWindow window(plugin.get(), 3, nullptr);
    QVERIFY(window.windowFlags() & Qt::WindowStaysOnTopHint);
}


void PluginListTest::pianoRollWindowStaysOnTop() {
    Project project;
    project.addMidiTrack("Midi 1");
    Track& track = project.tracks()[0];
    auto clip = std::make_shared<MidiClip>();
    clip->addNote(60, 100, 0, 960);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addMidiEvent(ev);
    const int64_t id = track.midiEvents().front().id();

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.openPianoRoll(0, id);

    QCOMPARE(window.m_pianoRollWindows.size(), size_t(1));
    QVERIFY(window.m_pianoRollWindows[0]->windowFlags() & Qt::WindowStaysOnTopHint);
}


QTEST_MAIN(PluginListTest)
#include "test_gui_plugins.moc"
