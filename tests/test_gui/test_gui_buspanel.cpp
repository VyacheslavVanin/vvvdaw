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

class BusPanelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void busPanelStripHasCompactControls();
    void busPanelStripsStayFixedWidth();
    void busPanelToggleRevealsCombinedPanel();
    void busPanelPanelStaysOpenAcrossRebuild();
    void busPanelListsHaveHeaderLabelsAndTopAdd();
    void busPanelSelection();
    void busPanelNameEditing();
    void busPanelFolderFoldUnfold();
    void busPanelContextMenuHasPutToFolder();
    void busPanelDropReorderWithinFolder();
    void busPanelDropTrailingKeepsFolder();
    void busPanelDropReorderFoldersInFolder();
    void busPanelDropFolderIntoFolder();
    void busPanelFolderTintNoIndent();
    void busPanelColorBarAssignsAndPropagates();
    void busPanelColorBarCtrlOverridesChildren();
    void busPanelSendAddAndRemove();
    void busPanelSendContextMenuRemovesSend();
    void busVolumeSliderFollowsMeterDbScale();
    void busLevelMeterIsNarrow();
    void panSliderHighlightsDeviationFromCenter();
    void sliderSizesAreIncreasedForUsability();
private:
    GuiTestEnv m_env;
};

void BusPanelTest::initTestCase() {
    if (!m_env.init())
        QSKIP("PortAudio not available");
}

void BusPanelTest::cleanupTestCase() {
    m_env.cleanup();
}

void BusPanelTest::busPanelStripHasCompactControls() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1)); // Master, Metronome, FX

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    // One vertical volume slider, level meter and plugin toggle per bus.
    const auto volumeSliders = window.m_busPanel->findChildren<QSlider*>("volumeSlider");
    QCOMPARE(volumeSliders.size(), 3);
    for (QSlider* s : volumeSliders)
        QCOMPARE(s->orientation(), Qt::Vertical);

    const auto meters = window.m_busPanel->findChildren<BusLevelMeter*>("levelMeter");
    QCOMPARE(meters.size(), 3);

    const auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    QCOMPARE(toggles.size(), 3);
    for (QPushButton* b : toggles)
        QVERIFY(b->isCheckable());

    // S/M buttons live below the name (one pair per bus).
    QCOMPARE(window.m_busPanel->findChildren<QPushButton*>("soloButton").size(), 3);
    QCOMPARE(window.m_busPanel->findChildren<QPushButton*>("muteButton").size(), 3);

    // The collapsed strip is much narrower than before.
    QWidget* strip = toggles[0]->parentWidget()->parentWidget();
    QVERIFY(strip->sizeHint().width() <= 100);

    // Plugin and send lists exist but are hidden until the panel is toggled.
    const auto lists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    QCOMPARE(lists.size(), 3);
    for (QWidget* l : lists)
        QVERIFY(!l->isVisible());
    const auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(sendLists.size(), 3);
    for (QWidget* l : sendLists)
        QVERIFY(!l->isVisible());
}


void BusPanelTest::busPanelStripsStayFixedWidth() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();

    auto stripWidths = [&window]() {
        std::vector<int> widths;
        const auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
        for (QPushButton* t : toggles)
            widths.push_back(t->parentWidget()->parentWidget()->width());
        return widths;
    };

    window.resize(500, 400);
    window.show();
    QCoreApplication::processEvents();
    const auto narrow = stripWidths();
    QCOMPARE(narrow.size(), 3);

    window.resize(1200, 400);
    QCoreApplication::processEvents();
    const auto wide = stripWidths();
    QCOMPARE(wide.size(), 3);

    // Strips must not stretch when the window widens.
    for (size_t i = 0; i < narrow.size(); ++i)
        QCOMPARE(wide[i], narrow[i]);
}


void BusPanelTest::busPanelToggleRevealsCombinedPanel() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    auto panels = window.m_busPanel->findChildren<QWidget*>("busFxPanel");
    auto lists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(toggles.size(), 3);
    QCOMPARE(panels.size(), 3);
    QCOMPARE(lists.size(), 3);
    QCOMPARE(sendLists.size(), 3);

    QWidget* strip = toggles[0]->parentWidget()->parentWidget();
    const int collapsedWidth = strip->sizeHint().width();

    // The combined panel is hidden and hides both the plugin and send lists.
    QVERIFY(!lists[0]->isVisible());
    QVERIFY(!sendLists[0]->isVisible());
    toggles[0]->click();
    QVERIFY(!panels[0]->isHidden());
    QVERIFY(lists[0]->isVisible());
    QVERIFY(sendLists[0]->isVisible());
    QVERIFY(strip->sizeHint().width() > collapsedWidth); // strip widens

    // Plugins are stacked above the sends.
    QVERIFY(lists[0]->mapTo(panels[0], QPoint(0, 0)).y()
            < sendLists[0]->mapTo(panels[0], QPoint(0, 0)).y());

    toggles[0]->click();
    QVERIFY(panels[0]->isHidden());
    QVERIFY(!lists[0]->isVisible());
    QVERIFY(!sendLists[0]->isVisible());
}


void BusPanelTest::busPanelPanelStaysOpenAcrossRebuild() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    auto lists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    QCOMPARE(toggles.size(), 3);
    QCOMPARE(lists.size(), 3);
    QVERIFY(!lists[2]->isVisible());
    toggles[2]->click();
    QVERIFY(lists[2]->isVisible());

    // A full panel rebuild (e.g. triggered after adding a plugin or send) must
    // not collapse the explicitly opened combined panel.
    window.m_busPanel->rebuild();
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    lists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(toggles.size(), 3);
    QCOMPARE(lists.size(), 3);
    QCOMPARE(sendLists.size(), 3);
    QVERIFY(toggles[2]->isChecked());
    QVERIFY(lists[2]->isVisible());
    QVERIFY(sendLists[2]->isVisible());

    // The other (never opened) panels stay collapsed.
    QVERIFY(!toggles[0]->isChecked());
    QVERIFY(!lists[0]->isVisible());
    QVERIFY(!toggles[1]->isChecked());
    QVERIFY(!lists[1]->isVisible());
}


void BusPanelTest::busPanelListsHaveHeaderLabelsAndTopAdd() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    toggles[2]->click(); // open the combined panel on the FX bus
    QCoreApplication::processEvents();

    auto pluginLists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(pluginLists.size(), 3);
    QCOMPARE(sendLists.size(), 3);
    PluginListWidget* plist = pluginLists[2];
    BusSendsWidget* slist = sendLists[2];

    // Header captions next to the add buttons.
    QLabel* effectsLabel = nullptr;
    QLabel* sendsLabel = nullptr;
    for (QLabel* lb : plist->findChildren<QLabel*>())
        if (lb->text().contains("Effects") && lb->isVisible()) { effectsLabel = lb; break; }
    for (QLabel* lb : slist->findChildren<QLabel*>())
        if (lb->text().contains("Sends") && lb->isVisible()) { sendsLabel = lb; break; }
    QVERIFY(effectsLabel);
    QVERIFY(sendsLabel);

    // The "+" buttons sit in the top half, on the same row as their label.
    QPushButton* plistAdd = nullptr;
    for (QPushButton* b : plist->findChildren<QPushButton*>())
        if (b->text() == "+") { plistAdd = b; break; }
    QPushButton* sendAdd = slist->findChild<QPushButton*>("sendAddButton");
    QVERIFY(plistAdd);
    QVERIFY(sendAdd);
    QVERIFY(plistAdd->y() < plist->height() / 2);
    QVERIFY(sendAdd->y() < slist->height() / 2);
    QCOMPARE(plistAdd->y(), effectsLabel->y());
    QCOMPARE(sendAdd->y(), sendsLabel->y());
}

// Send a left-button press to a widget (used to drive strip selection).
static void pressLeft(QWidget* w, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), w->mapToGlobal(QPoint(5, 5)),
                      Qt::LeftButton, Qt::LeftButton, mods);
    QApplication::sendEvent(w, &press);
}


void BusPanelTest::busPanelSelection() {
    Project project;
    AudioBus b1;
    b1.setName("B1");
    project.addBus(std::move(b1)); // index 2
    AudioBus b2;
    b2.setName("B2");
    project.addBus(std::move(b2)); // index 3

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    QCOMPARE(toggles.size(), 4); // Master, Metronome, B1, B2
    QWidget* c2 = toggles[2]->parentWidget(); // B1 controls
    QWidget* c3 = toggles[3]->parentWidget(); // B2 controls

    // Plain click selects a single bus.
    pressLeft(c2);
    auto sel = window.m_busPanel->selectedBusIndices();
    QCOMPARE(sel.size(), size_t(1));
    QCOMPARE(sel[0], 2);

    // Ctrl-click adds another bus to the selection.
    pressLeft(c3, Qt::ControlModifier);
    sel = window.m_busPanel->selectedBusIndices();
    QCOMPARE(sel.size(), size_t(2));
    QVERIFY(std::find(sel.begin(), sel.end(), 2) != sel.end());
    QVERIFY(std::find(sel.begin(), sel.end(), 3) != sel.end());

    // Ctrl-click again removes it.
    pressLeft(c3, Qt::ControlModifier);
    sel = window.m_busPanel->selectedBusIndices();
    QCOMPARE(sel.size(), size_t(1));
    QCOMPARE(sel[0], 2);

    // Re-anchor on bus 2, then shift-click selects the range 2..3.
    pressLeft(c2);
    pressLeft(c3, Qt::ShiftModifier);
    sel = window.m_busPanel->selectedBusIndices();
    QCOMPARE(sel.size(), size_t(2));
    QVERIFY(std::find(sel.begin(), sel.end(), 2) != sel.end());
    QVERIFY(std::find(sel.begin(), sel.end(), 3) != sel.end());
}


void BusPanelTest::busPanelNameEditing() {
    Project project;
    AudioBus b1;
    b1.setName("B1");
    project.addBus(std::move(b1)); // index 2

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    auto edits = window.m_busPanel->findChildren<QLineEdit*>();
    QVERIFY(edits.size() >= 3);
    QLineEdit* name = edits[2]; // B1
    QVERIFY(name->isReadOnly());

    // Double-click makes the name editable (cursor appears).
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(5, 5), QPointF(5, 5),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(name, &dbl);
    QVERIFY(!name->isReadOnly());
}


void BusPanelTest::busPanelFolderFoldUnfold() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2
    AudioBus child;
    child.setName("Child");
    project.addBus(std::move(child)); // index 3
    project.buses()[3].setOutputBusIndex(2); // Child routes into Folder

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    // Folder is unfolded by default: all four strips are built.
    QCOMPARE(window.m_busPanel->findChildren<QWidget*>("busStrip").size(), 4);
    auto folderToggles = window.m_busPanel->findChildren<QPushButton*>("folderToggle");
    QCOMPARE(folderToggles.size(), 1); // only the folder bus

    // Collapse the folder: the child strip disappears.
    folderToggles[0]->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(window.m_busPanel->findChildren<QWidget*>("busStrip").size(), 3);

    // Unfold again: the child returns.
    folderToggles = window.m_busPanel->findChildren<QPushButton*>("folderToggle");
    QCOMPARE(folderToggles.size(), 1);
    folderToggles[0]->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(window.m_busPanel->findChildren<QWidget*>("busStrip").size(), 4);
}


void BusPanelTest::busPanelContextMenuHasPutToFolder() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2
    AudioBus child;
    child.setName("Child");
    project.addBus(std::move(child)); // index 3

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    QCoreApplication::processEvents();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    QWidget* c3 = toggles[3]->parentWidget(); // Child controls

    // Open the context menu and inspect its actions.
    bool sawPutToFolder = false;
    QTimer::singleShot(0, [&] {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* m = qobject_cast<QMenu*>(w)) {
                for (QAction* a : m->actions())
                    if (a->text().contains("Put to folder"))
                        sawPutToFolder = true;
                m->close();
            }
        }
    });

    QContextMenuEvent ev(QContextMenuEvent::Mouse, c3->rect().center(),
                         c3->mapToGlobal(c3->rect().center()));
    QApplication::sendEvent(c3, &ev);
    QCoreApplication::processEvents();
    QVERIFY(sawPutToFolder);
}


void BusPanelTest::busPanelDropReorderWithinFolder() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2
    AudioBus a;
    a.setName("A");
    project.addBus(std::move(a)); // index 3
    AudioBus b;
    b.setName("B");
    project.addBus(std::move(b)); // index 4
    project.buses()[3].setOutputBusIndex(2); // A -> Folder
    project.buses()[4].setOutputBusIndex(2); // B -> Folder
    project.setBusDisplayOrder({ 0, 1, 2, 4, 3 }); // B renders before A

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto strips = window.m_busPanel->findChildren<QWidget*>("busStrip");
    QCOMPARE(strips.size(), 5); // Master, Metronome, Folder, B, A

    // Drop B (4) on A's (3) right half: B is inserted after A inside the folder.
    QWidget* aStrip = strips[4]; // last in render order
    QPoint dropPos = aStrip->geometry().center();
    dropPos.rx() += aStrip->width() / 4;

    window.m_busPanel->handleBusDrop(dropPos, { 4 });
    QCoreApplication::processEvents();

    // B stayed in the folder and now sorts after A.
    QCOMPARE(project.buses()[4].outputBusIndex(), 2);
    const auto& order = project.busDisplayOrder();
    auto itA = std::find(order.begin(), order.end(), 3);
    auto itB = std::find(order.begin(), order.end(), 4);
    QVERIFY(itA != order.end() && itB != order.end());
    QVERIFY(itA < itB);
}


void BusPanelTest::busPanelDropTrailingKeepsFolder() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2
    AudioBus a;
    a.setName("A");
    project.addBus(std::move(a)); // index 3
    AudioBus b;
    b.setName("B");
    project.addBus(std::move(b)); // index 4
    project.buses()[3].setOutputBusIndex(2); // A -> Folder
    project.buses()[4].setOutputBusIndex(2); // B -> Folder

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    QWidget* container = window.m_busPanel->findChild<QWidget*>("busContainer");
    QVERIFY(container);

    // Drop B on the empty space beyond all strips: it must NOT be ejected.
    QPoint dropPos(container->width() - 5, 10);
    window.m_busPanel->handleBusDrop(dropPos, { 4 });
    QCoreApplication::processEvents();

    QCOMPARE(project.buses()[4].outputBusIndex(), 2); // still in the folder
}


void BusPanelTest::busPanelDropReorderFoldersInFolder() {
    Project project;
    AudioBus o;
    o.setName("O");
    project.addBus(std::move(o)); // index 2
    AudioBus f1;
    f1.setName("F1");
    project.addBus(std::move(f1)); // index 3
    AudioBus f2;
    f2.setName("F2");
    project.addBus(std::move(f2)); // index 4
    AudioBus c1;
    c1.setName("c1");
    project.addBus(std::move(c1)); // index 5
    project.buses()[3].setOutputBusIndex(2); // F1 -> O
    project.buses()[4].setOutputBusIndex(2); // F2 -> O
    project.buses()[5].setOutputBusIndex(3); // c1 -> F1

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto strips = window.m_busPanel->findChildren<QWidget*>("busStrip");
    QCOMPARE(strips.size(), 6); // Master, Metro, O, F1, c1, F2
    QWidget* f1Strip = strips[3];
    QWidget* f2Strip = strips[5];

    // Drop F2 on F1's left quarter: F2 becomes F1's sibling (still in O).
    QPoint dropPos(f1Strip->geometry().left() + f1Strip->width() / 8,
                   f1Strip->geometry().center().y());
    window.m_busPanel->handleBusDrop(dropPos, { 4 });
    QCoreApplication::processEvents();

    QCOMPARE(project.buses()[4].outputBusIndex(), 2); // still in O, not inside F1
    const auto& order = project.busDisplayOrder();
    auto itF1 = std::find(order.begin(), order.end(), 3);
    auto itF2 = std::find(order.begin(), order.end(), 4);
    QVERIFY(itF1 != order.end() && itF2 != order.end());
    QVERIFY(itF2 < itF1); // F2 is now before F1
}


void BusPanelTest::busPanelDropFolderIntoFolder() {
    Project project;
    AudioBus o;
    o.setName("O");
    project.addBus(std::move(o)); // index 2
    AudioBus f1;
    f1.setName("F1");
    project.addBus(std::move(f1)); // index 3
    AudioBus f2;
    f2.setName("F2");
    project.addBus(std::move(f2)); // index 4
    AudioBus c1;
    c1.setName("c1");
    project.addBus(std::move(c1)); // index 5
    project.buses()[3].setOutputBusIndex(2); // F1 -> O
    project.buses()[4].setOutputBusIndex(2); // F2 -> O
    project.buses()[5].setOutputBusIndex(3); // c1 -> F1

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto strips = window.m_busPanel->findChildren<QWidget*>("busStrip");
    QCOMPARE(strips.size(), 6);
    QWidget* f1Strip = strips[3];

    // Drop F2 on F1's body: F2 moves into F1 (nesting is still possible).
    QPoint dropPos = f1Strip->geometry().center();
    window.m_busPanel->handleBusDrop(dropPos, { 4 });
    QCoreApplication::processEvents();

    QCOMPARE(project.buses()[4].outputBusIndex(), 3); // F2 is now inside F1
}


void BusPanelTest::busPanelFolderTintNoIndent() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2 (F)
    AudioBus child;
    child.setName("Child");
    project.addBus(std::move(child)); // index 3 (C) -> F
    AudioBus nested;
    nested.setName("Nested");
    project.addBus(std::move(nested)); // index 4 (N) -> F
    AudioBus grandchild;
    grandchild.setName("Grandchild");
    project.addBus(std::move(grandchild)); // index 5 (G) -> N
    AudioBus top;
    top.setName("Top");
    project.addBus(std::move(top)); // index 6 (T) -> master
    project.buses()[3].setOutputBusIndex(2); // C -> F
    project.buses()[4].setOutputBusIndex(2); // N -> F
    project.buses()[5].setOutputBusIndex(4); // G -> N
    project.buses()[6].setOutputBusIndex(0); // T -> master

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    // Render order: Master(0), Metronome(1), Folder(2), Child(3), Nested(4),
    // Grandchild(5), Top(6).
    auto strips = window.m_busPanel->findChildren<QWidget*>("busStrip");
    QCOMPARE(strips.size(), 7);
    QWidget* folderStrip = strips[2];
    QWidget* childStrip = strips[3];
    QWidget* nestedStrip = strips[4];
    QWidget* grandchildStrip = strips[5];
    QWidget* topStrip = strips[6];

    // No indentation: every strip sits edge to edge with only the container's
    // plain spacing (the old code inserted 14px/28px indent spacers per level,
    // so folder/child gaps were far wider than the top-level gap).
    int plainGap = strips[1]->geometry().left() - strips[0]->geometry().right();
    QVERIFY(plainGap < 10); // a sane plain layout spacing
    for (int i = 1; i + 1 < strips.size(); ++i)
        QCOMPARE(strips[i + 1]->geometry().left() - strips[i]->geometry().right(),
                 plainGap);

    // Folder membership via a shared tint: the folder and its direct children
    // share one tone, a nested folder forms its own distinct group.
    auto stripColor = [](QWidget* w) { return w->palette().color(QPalette::Window); };
    QVERIFY(project.isBusFolder(2));
    QCOMPARE(stripColor(childStrip), stripColor(folderStrip));      // C shares F's tint
    QCOMPARE(stripColor(grandchildStrip), stripColor(nestedStrip)); // G shares N's tint
    QVERIFY(stripColor(nestedStrip) != stripColor(folderStrip));    // nested folder: own hue
    QVERIFY(stripColor(grandchildStrip) != stripColor(childStrip)); // nested group distinct

    // Top-level buses keep the plain alternating tone (no tint).
    QCOMPARE(stripColor(topStrip), QColor("#2e2e2e"));
    QCOMPARE(stripColor(strips[1]), QColor("#333333")); // Metronome
}


void BusPanelTest::busPanelColorBarAssignsAndPropagates() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2 (F) -> master
    AudioBus child;
    child.setName("Child");
    project.addBus(std::move(child)); // index 3 (C) -> F
    project.buses()[3].setOutputBusIndex(2);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    QCoreApplication::processEvents();

    // Render order: Master(0), Metronome(1), Folder(2), Child(3).
    auto bars = window.m_busPanel->findChildren<BusColorBar*>("busColorBar");
    QCOMPARE(bars.size(), 4);
    for (BusColorBar* b : bars)
        QVERIFY(b->height() >= 4 && b->height() <= 6); // ~5px tall strip

    // Before any manual color the bars show the automatic/effective color.
    QCOMPARE(bars[2]->color(), project.busColor(2));
    QCOMPARE(bars[3]->color(), project.busColor(3));

    // Stub out the modal picker and assign a color to the folder.
    bars[2]->setColorPickerForTesting([](const QColor&) { return QColor("#ff0000"); });
    QSignalSpy spy(window.m_busPanel, &BusPanelWidget::busColorWillChange);
    bars[2]->pickColor();

    QVERIFY(project.buses()[2].colorSet());
    QCOMPARE(project.buses()[2].color(), QColor("#ff0000"));
    // The child keeps no manual color but inherits the folder's color.
    QVERIFY(!project.buses()[3].colorSet());
    QCOMPARE(project.busColor(3), QColor("#ff0000"));
    QCOMPARE(spy.count(), 1);

    // The panel refreshed the bars to the new color. The assignment may have
    // triggered a rebuild that scheduled the old bars for deletion; flush them
    // before re-fetching.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto refreshed = window.m_busPanel->findChildren<BusColorBar*>("busColorBar");
    QCOMPARE(refreshed[2]->color(), QColor("#ff0000"));

    // Undo restores the automatic color.
    window.performUndo();
    QVERIFY(!project.buses()[2].colorSet());
}


void BusPanelTest::busPanelColorBarCtrlOverridesChildren() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2 (F)
    AudioBus child;
    child.setName("Child");
    project.addBus(std::move(child)); // index 3 (C) -> F
    AudioBus grandchild;
    grandchild.setName("Grand");
    project.addBus(std::move(grandchild)); // index 4 (G) -> C
    project.buses()[3].setOutputBusIndex(2);
    project.buses()[4].setOutputBusIndex(3);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    QCoreApplication::processEvents();

    auto bars = window.m_busPanel->findChildren<BusColorBar*>("busColorBar");
    QCOMPARE(bars.size(), 5);
    // Folder is index 2 -> bars[2].
    bars[2]->setColorPickerForTesting([](const QColor&) { return QColor("#00ff00"); });

    // Give the child a manual color first, so we can verify Ctrl clears it.
    project.buses()[3].setColor(QColor("#101010"));
    QVERIFY(project.buses()[3].colorSet());

    // Ctrl+click assigns the folder's color and clears the manual-color flag on
    // all descendants, so they inherit the folder's color.
    QTest::mouseClick(bars[2], Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(project.buses()[2].color(), QColor("#00ff00"));
    QVERIFY(project.buses()[2].colorSet());
    QVERIFY(!project.buses()[3].colorSet()); // flag cleared
    QVERIFY(!project.buses()[4].colorSet());
    QCOMPARE(project.busColor(3), QColor("#00ff00")); // inherits the folder
    QCOMPARE(project.busColor(4), QColor("#00ff00")); // inherits recursively

    // A single undo reverts the whole override, restoring the child's manual
    // color.
    window.performUndo();
    QVERIFY(!project.buses()[2].colorSet());
    QVERIFY(project.buses()[3].colorSet());              // child's manual color back
    QCOMPARE(project.buses()[3].color(), QColor("#101010"));
    QVERIFY(!project.buses()[4].colorSet());
}


void BusPanelTest::busPanelSendAddAndRemove() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    toggles[2]->click(); // open the combined panel on the FX bus
    QVERIFY(project.buses()[2].sends().empty());

    auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(sendLists.size(), 3);
    auto* addBtn = sendLists[2]->findChild<QPushButton*>("sendAddButton");
    QVERIFY(addBtn);
    addBtn->click();
    QCOMPARE(project.buses()[2].sends().size(), size_t(1));
    QCOMPARE(project.buses()[2].sends()[0].busIndex, 0);
    QCOMPARE(project.buses()[2].sends()[0].preFader, false);

    // The add rebuilt the panel; flush deferred deletes and re-fetch.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(sendLists.size(), 3);
    BusSendsWidget* sendList = sendLists[2];

    const auto combos = sendList->findChildren<QComboBox*>("sendTargetCombo");
    QCOMPARE(combos.size(), 1);
    const auto levelSliders = sendList->findChildren<QSlider*>("sendLevelSlider");
    QCOMPARE(levelSliders.size(), 1);
    const auto preToggles = sendList->findChildren<QPushButton*>("sendPreToggle");
    QCOMPARE(preToggles.size(), 1);

    // Changing the level slider (dB scale) updates the model.
    levelSliders[0]->setValue(50); // -30 dB
    QVERIFY(std::abs(project.buses()[2].sends()[0].level - decibelsToLinear(-30.0f)) < 1e-3f);

    // The Pre/Post toggle flips the pre-fader flag.
    preToggles[0]->click();
    QCOMPARE(project.buses()[2].sends()[0].preFader, true);

    // Undoing the level, pre and add commands restores the empty list.
    while (project.buses()[2].sends().size() > 0)
        window.performUndo();
    QCOMPARE(project.buses()[2].sends().size(), size_t(0));
}


void BusPanelTest::busPanelSendContextMenuRemovesSend() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));
    AudioBus::Send s;
    s.busIndex = 0;
    project.buses()[2].sends().push_back(s);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    QCoreApplication::processEvents();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    toggles[2]->click(); // open the combined panel on the FX bus
    QCoreApplication::processEvents();

    auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(sendLists.size(), 3);
    BusSendsWidget* sendList = sendLists[2];
    QCOMPARE(project.buses()[2].sends().size(), size_t(1));

    QWidget* row = sendList->findChild<QWidget*>("sendRow");
    QVERIFY(row);

    // Drive the context menu: open it on the row and trigger "Remove Send".
    QTimer::singleShot(0, [] {
        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* m = qobject_cast<QMenu*>(w)) {
                for (QAction* a : m->actions())
                    if (a->text().contains("Remove Send")) { menu = m; break; }
            }
            if (menu) break;
        }
        if (!menu) return;
        for (QAction* a : menu->actions()) {
            if (a->text().contains("Remove Send")) {
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

    QCOMPARE(project.buses()[2].sends().size(), size_t(0));
}


void BusPanelTest::busVolumeSliderFollowsMeterDbScale() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    b1.setVolume(0.5f); // -6.02 dB
    project.addBus(std::move(b1)); // Master, Metronome, FX

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    const auto sliders = window.m_busPanel->findChildren<QSlider*>("volumeSlider");
    QCOMPARE(sliders.size(), 3);
    QSlider* slider = sliders[2]; // third row = FX bus
    AudioBus& fx = window.m_project.buses()[2];

    // The slider position is derived from the linear volume through the meter's
    // dB scale: 0.5 (-6.02 dB) sits ~90 of 100.
    QVERIFY(std::abs(slider->value() - 90) <= 1);

    // Full scale = 0 dB = unity.
    slider->setValue(100);
    QCOMPARE(fx.volume(), 1.0f);

    // Bottom of the fader = -60 dB = silence.
    slider->setValue(0);
    QCOMPARE(fx.volume(), 0.0f);

    // Midpoint = -30 dB, matching the same point on the meter scale.
    slider->setValue(50);
    QVERIFY(std::abs(fx.volume() - decibelsToLinear(-30.0f)) < 1e-3f);
}


void BusPanelTest::busLevelMeterIsNarrow() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    const auto meters = window.m_busPanel->findChildren<BusLevelMeter*>("levelMeter");
    QCOMPARE(meters.size(), 3);
    for (BusLevelMeter* m : meters)
        QVERIFY(m->maximumWidth() < 34); // indicator bar column is narrower

    // Painting the dB scale plus the volume marker must not crash.
    BusLevelMeter* meter = meters[0];
    meter->resize(30, 120);
    meter->setPeak(0.5f);
    meter->setVolume(0.5f);
    meter->setClipping(true);
    QPixmap pm = meter->grab();
    QVERIFY(!pm.isNull());
}


void BusPanelTest::panSliderHighlightsDeviationFromCenter() {
    PanSlider slider;
    slider.setRange(-100, 100);
    slider.resize(100, PanSlider::kHeight);

    // At the center value the highlight collapses to a degenerate span.
    slider.setValue(0);
    const QRect zero = slider.highlightRect();
    QCOMPARE(zero.width(), 0);
    const int center = zero.left();
    QCOMPARE(slider.toolTip(), QString("Pan: Center"));

    // A positive value highlights the span from the center to the right.
    slider.setValue(50);
    const QRect right = slider.highlightRect();
    QVERIFY(right.width() > 4);
    QCOMPARE(right.left(), center);
    QVERIFY(right.center().x() > center);
    QCOMPARE(slider.toolTip(), QString("Pan: R 50%"));

    // A negative value highlights the span from the left to the center, an
    // exact mirror of the positive case around the same center point.
    slider.setValue(-50);
    const QRect left = slider.highlightRect();
    QVERIFY(left.width() > 4);
    QCOMPARE(left.right(), center - 1);
    QVERIFY(left.center().x() < center);
    QCOMPARE(slider.toolTip(), QString("Pan: L 50%"));

    QVERIFY(std::abs(left.width() - right.width()) <= 1);
    QCOMPARE(left.height(), right.height());
    QCOMPARE(left.bottom(), right.bottom());
}


void BusPanelTest::sliderSizesAreIncreasedForUsability() {
    Project project;
    project.addTrack("Audio 1");
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.m_instrumentPanel->rebuild();
    window.show();
    QCoreApplication::processEvents();

    constexpr int kMinSliderExtent = 16;

    // Track panel: horizontal pan + volume sliders are taller.
    QCOMPARE(window.m_trackRows.size(), size_t(1));
    TrackPanelWidget* trackPanel = window.m_trackRows[0].panel;
    const auto trackSliders = trackPanel->findChildren<QSlider*>();
    QCOMPARE(trackSliders.size(), 2); // pan + volume
    for (QSlider* s : trackSliders)
        QVERIFY2(s->height() >= kMinSliderExtent, "track slider too small");

    // Volume slider tooltip reports the current percentage.
    QSlider* trackVol = nullptr;
    QSlider* trackPan = nullptr;
    for (QSlider* s : trackSliders) {
        if (s->maximum() == 100 && s->minimum() == 0) trackVol = s;
        else trackPan = s;
    }
    QVERIFY(trackVol);
    QVERIFY(trackPan);
    trackVol->setValue(75);
    QCOMPARE(trackVol->toolTip(), QString("Volume: 75%"));

    // Bus panel: vertical volume fader is wider, pan slider is taller.
    const auto busVol = window.m_busPanel->findChildren<QSlider*>("volumeSlider");
    QCOMPARE(busVol.size(), 3);
    for (QSlider* s : busVol)
        QVERIFY2(s->width() >= 28, "bus volume slider too narrow");

    // Bus volume fader tooltip reports the dB value (midpoint = -30 dB).
    busVol[0]->setValue(50);
    QCOMPARE(busVol[0]->toolTip(), QString("Volume: -30.0 dB"));
    const auto busPan = window.m_busPanel->findChildren<PanSlider*>();
    QCOMPARE(busPan.size(), 3);
    for (PanSlider* s : busPan)
        QVERIFY2(s->height() >= kMinSliderExtent, "bus pan slider too small");

    // Instrument panel: horizontal pan + volume sliders are taller.
    const auto instSliders = window.m_instrumentPanel->findChildren<QSlider*>();
    QCOMPARE(instSliders.size(), 2); // pan + volume
    for (QSlider* s : instSliders)
        QVERIFY2(s->height() >= kMinSliderExtent, "instrument slider too small");
}


QTEST_MAIN(BusPanelTest)
#include "test_gui_buspanel.moc"
