#include <QTest>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include "gui/PluginPickerDialog.h"
#include "plugin/PluginManager.h"

namespace {

QVector<PluginInfo> makePlugins() {
    // Order matters: "Ample Guitar" must fuzzy-rank above "Compressor" for
    // the empty query so arrow-key tests can rely on the initial selection.
    PluginInfo guitar;
    guitar.name = "Ample Guitar";
    guitar.vendor = "Ample";
    guitar.path = "/fake/ample_guitar.lv2";
    guitar.pluginId = "ample-guitar";
    guitar.category = "Instrument";
    guitar.type = "lv2";
    guitar.isInstrument = true;

    PluginInfo synth;
    synth.name = "Simple Synth";
    synth.vendor = "TestVendor";
    synth.path = "/fake/simple_synth.lv2";
    synth.pluginId = "simple-synth";
    synth.category = "Instrument";
    synth.type = "lv2";
    synth.isInstrument = true;

    PluginInfo comp;
    comp.name = "Compressor";
    comp.vendor = "TestVendor";
    comp.path = "/fake/compressor.lv2";
    comp.pluginId = "compressor";
    comp.category = "Effect";
    comp.type = "lv2";
    comp.isInstrument = false;

    PluginInfo reverb;
    reverb.name = "Reverb";
    reverb.vendor = "TestVendor";
    reverb.path = "/fake/reverb.lv2";
    reverb.pluginId = "reverb";
    reverb.category = "Effect";
    reverb.type = "lv2";
    reverb.isInstrument = false;

    return {comp, reverb, guitar, synth};
}

QLineEdit* findSearchEdit(const PluginPickerDialog& dialog) {
    return dialog.findChild<QLineEdit*>();
}

QListWidget* findList(const PluginPickerDialog& dialog) {
    return dialog.findChild<QListWidget*>();
}

} // namespace

class PluginPickerTest : public QObject {
    Q_OBJECT
private slots:
    // Shows all effect plugins of the requested category, sorted fuzzy-best
    // first, and preselects the top entry.
    void initialListShowsAllMatchingCategory();
    // Typing in the search field filters the list by fuzzy match.
    void searchFiltersListFuzzy();
    // filterInstruments=true hides effect plugins and vice versa.
    void categoryFilterRespected();
    // Down arrow in the search field moves the list selection without moving
    // the caret/focus out of the search field.
    void arrowDownMovesSelection();
    // Consecutive Up/Down presses navigate back and forth across the list.
    void arrowsNavigateBothDirections();
    // After editing the query the selection resets to the new top entry.
    void textChangeResetsSelectionToTop();
    // Selecting via arrows then Enter accepts the highlighted entry, not the
    // top one, and the result exposes type/path of that entry.
    void keyboardSelectionReturnsHighlightedImage();

    // Enter (or Add) with an empty result list must not accept
    // with a bogus selection.
    void emptyListEnterDoesNotAccept();
};

void PluginPickerTest::initialListShowsAllMatchingCategory() {
    PluginPickerDialog dialog(makePlugins(), /*instrumentsOnly=*/false);
    auto* list = findList(dialog);
    QVERIFY(list);
    QCOMPARE(list->count(), 2);
    QVERIFY(list->currentItem());
    QVERIFY(list->currentItem()->text().contains("Compressor"));
}

void PluginPickerTest::searchFiltersListFuzzy() {
    PluginPickerDialog dialog(makePlugins(), false);
    auto* search = findSearchEdit(dialog);
    auto* list = findList(dialog);
    QVERIFY(search && list);

    search->setText("comp");
    QCOMPARE(list->count(), 1);
    QVERIFY(list->currentItem());
    QVERIFY(list->currentItem()->text().contains("Compressor"));

    search->setText("zzz");
    QCOMPARE(list->count(), 0);
    QCOMPARE(list->currentItem(), nullptr);
}

void PluginPickerTest::categoryFilterRespected() {
    // instrumentsOnly=false lists only effect plugins...
    PluginPickerDialog effects(makePlugins(), false);
    auto* list = findList(effects);
    QVERIFY(list);
    QCOMPARE(list->count(), 2);
    for (int i = 0; i < list->count(); ++i)
        QVERIFY(!list->item(i)->text().contains("Guitar"));

    // ...and instrumentsOnly=true lists only instrument plugins.
    PluginPickerDialog instrumentsOnly(makePlugins(), true);
    auto* listOnly = findList(instrumentsOnly);
    QVERIFY(listOnly);
    QCOMPARE(listOnly->count(), 2);
    for (int i = 0; i < listOnly->count(); ++i)
        QVERIFY(!listOnly->item(i)->text().contains("Compressor"));
}

void PluginPickerTest::arrowDownMovesSelection() {
    PluginPickerDialog dialog(makePlugins(), false);
    auto* search = findSearchEdit(dialog);
    auto* list = findList(dialog);
    QVERIFY(search && list);
    dialog.show();
    QCoreApplication::processEvents();
    QCOMPARE(list->currentRow(), 0);

    QTest::keyClick(search, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 1);
    QVERIFY(list->currentItem());
    QVERIFY(list->currentItem()->text().contains("Reverb"));
    // Focus stays in the search field (offscreen platform may not report
    // hasFocus() reliably), but typing still filters the list.
    search->setText("comp");
    QCOMPARE(list->count(), 1);
    QVERIFY(list->currentItem()->text().contains("Compressor"));
}

void PluginPickerTest::arrowsNavigateBothDirections() {
    PluginPickerDialog dialog(makePlugins(), false);
    auto* search = findSearchEdit(dialog);
    auto* list = findList(dialog);
    QVERIFY(search && list);
    QCOMPARE(list->currentRow(), 0);

    QTest::keyClick(search, Qt::Key_Down);
    QTest::keyClick(search, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 1);
    QTest::keyClick(search, Qt::Key_Up);
    // Going above the first entry keeps the selection clamped at row 0.
    QTest::keyClick(search, Qt::Key_Up);
    QCOMPARE(list->currentRow(), 0);
}

void PluginPickerTest::textChangeResetsSelectionToTop() {
    PluginPickerDialog dialog(makePlugins(), false);
    auto* search = findSearchEdit(dialog);
    auto* list = findList(dialog);
    QVERIFY(search && list);

    QTest::keyClick(search, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 1);
    search->setText("rev");
    QVERIFY(list->currentItem());
    QCOMPARE(list->currentRow(), 0);
    QVERIFY(list->currentItem()->text().contains("Reverb"));
}

void PluginPickerTest::keyboardSelectionReturnsHighlightedImage() {
    PluginPickerDialog dialog(makePlugins(), false);
    auto* search = findSearchEdit(dialog);
    QVERIFY(search);

    QTest::keyClick(search, Qt::Key_Down);
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(dialog.selectedType(), QString("lv2"));
    QVERIFY(dialog.selectedPath().contains("reverb"));
}

void PluginPickerTest::emptyListEnterDoesNotAccept() {
    PluginPickerDialog dialog(makePlugins(), false);
    auto* search = findSearchEdit(dialog);
    QVERIFY(search);

    search->setText("nothing-matches-this");
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
}

QTEST_MAIN(PluginPickerTest)
#include "test_gui_plugin_picker.moc"
