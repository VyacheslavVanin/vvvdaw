#include <QTest>
#include <QApplication>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QMenu>
#include <QAction>
#include <QImage>
#include <QPainter>

#include "gui/TimelineRuler.h"
#include "core/Constants.h"

// Subclass that avoids the modal exec() in popupCreateMenu/popupRemoveMenu so
// the drag/click gesture can be driven from the test, and exposes the menu
// builders so the actions can be triggered directly.
class TestTimelineRuler : public TimelineRuler {
public:
    using TimelineRuler::TimelineRuler;
    using TimelineRuler::setScrollOffset;
    using TimelineRuler::setZoom;

    int createCalls = 0;
    int removeCalls = 0;
    int64_t lastCreateStart = -1;
    int64_t lastCreateEnd = -1;

    QMenu* menuCreate(int64_t s, int64_t e) { return buildCreateMenu(s, e); }
    QMenu* menuRemove() { return buildRemoveMenu(); }

protected:
    void popupCreateMenu(const QPoint&, int64_t start, int64_t end) override {
        ++createCalls;
        lastCreateStart = start;
        lastCreateEnd = end;
    }
    void popupRemoveMenu(const QPoint&) override { ++removeCalls; }
};

namespace {

// Right-button press, optional move, release. Sending the events directly (via
// QApplication::sendEvent) keeps the button state stable during the drag,
// unlike QTest::mouseMove which drops the pressed-button flag.
void sendRightDrag(QWidget* w, int fromX, int toX) {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(fromX, 5.0), w->mapToGlobal(QPoint(fromX, 5)),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    if (toX != fromX) {
        QMouseEvent move(QEvent::MouseMove, QPointF(toX, 5.0), w->mapToGlobal(QPoint(toX, 5)),
                         Qt::NoButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(w, &move);
    }
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(toX, 5.0), w->mapToGlobal(QPoint(toX, 5)),
                        Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &release);
}

} // namespace

class RulerTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void dragSelectReportsRange();
    void dragSelectAcknowledgesPreview();
    void createLoopMenuSetsAndReplacesLoop();
    void createRecordRegionMenuSetsAndReplaces();
    void createBothMenuSetsLoopAndRecordRegion();
    void plainClickOffersOnlyRemove();
    void degenerateDragFallsBackToRemove();

private:
    TestTimelineRuler* makeRuler();
    TestTimelineRuler* m_ruler = nullptr;
};

TestTimelineRuler* RulerTest::makeRuler() {
    auto* r = new TestTimelineRuler;
    r->resize(400, 28);
    r->setScrollOffset(0);
    r->setZoom(1.0); // 1 px == 1 sample
    r->setSampleRate(48000);
    r->setSnapToGrid(false);
    return r;
}

void RulerTest::init() {
    m_ruler = makeRuler();
}

void RulerTest::cleanup() {
    delete m_ruler;
    m_ruler = nullptr;
}

void RulerTest::dragSelectReportsRange() {
    sendRightDrag(m_ruler, 100, 180);
    QCOMPARE(m_ruler->createCalls, 1);
    QCOMPARE(m_ruler->lastCreateStart, int64_t(100));
    QCOMPARE(m_ruler->lastCreateEnd, int64_t(180));
    QCOMPARE(m_ruler->removeCalls, 0);
}

void RulerTest::dragSelectAcknowledgesPreview() {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(100, 5.0), m_ruler->mapToGlobal(QPoint(100, 5)),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(m_ruler, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(180, 5.0), m_ruler->mapToGlobal(QPoint(180, 5)),
                     Qt::NoButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(m_ruler, &move);

    QVERIFY(m_ruler->isSelecting());

    // Render the live preview: a translucent blue highlight drawn during the
    // drag. At zoom 1.0 with scroll 0 there are no grid ticks under x=400, so a
    // pixel between the selection edges shows the highlight fill (blue ~100,
    // background is ~42), while the edges carry the stronger border color.
    QImage img(m_ruler->size(), QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    m_ruler->render(&img);
    QVERIFY(img.pixelColor(150, 14).blue() > 80);
    QVERIFY(img.pixelColor(100, 14).blue() > 150);
    QVERIFY(img.pixelColor(180, 14).blue() > 150);

    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(180, 5.0), m_ruler->mapToGlobal(QPoint(180, 5)),
                        Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(m_ruler, &release);
    QVERIFY(!m_ruler->isSelecting());
    QCOMPARE(m_ruler->createCalls, 1);
}

void RulerTest::createLoopMenuSetsAndReplacesLoop() {
    m_ruler->setLoop(5000, 10000); // an existing loop is replaced by a new drag
    QSignalSpy spy(m_ruler, &TimelineRuler::loopChanged);

    QMenu* menu = m_ruler->menuCreate(100, 200);
    QAction* create = nullptr;
    for (QAction* a : menu->actions())
        if (a->text() == "Create Loop") { create = a; break; }
    QVERIFY(create);
    create->trigger();

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toLongLong(), int64_t(100));
    QCOMPARE(args.at(1).toLongLong(), int64_t(200));
    QCOMPARE(m_ruler->loopStart(), int64_t(100));
    QCOMPARE(m_ruler->loopEnd(), int64_t(200));
}

void RulerTest::createRecordRegionMenuSetsAndReplaces() {
    m_ruler->setRecordRegion(5000, 10000); // an existing region is replaced
    QSignalSpy spy(m_ruler, &TimelineRuler::recordRegionChanged);

    QMenu* menu = m_ruler->menuCreate(150, 300);
    QAction* create = nullptr;
    for (QAction* a : menu->actions())
        if (a->text() == "Create Record Region") { create = a; break; }
    QVERIFY(create);
    create->trigger();

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toLongLong(), int64_t(150));
    QCOMPARE(args.at(1).toLongLong(), int64_t(300));
    QCOMPARE(m_ruler->recordRegionStart(), int64_t(150));
    QCOMPARE(m_ruler->recordRegionEnd(), int64_t(300));
}

void RulerTest::createBothMenuSetsLoopAndRecordRegion() {
    QSignalSpy loopSpy(m_ruler, &TimelineRuler::loopChanged);
    QSignalSpy rrSpy(m_ruler, &TimelineRuler::recordRegionChanged);

    QMenu* menu = m_ruler->menuCreate(100, 250);
    QAction* create = nullptr;
    for (QAction* a : menu->actions())
        if (a->text() == "Create Loop and Record Region") { create = a; break; }
    QVERIFY(create);
    create->trigger();

    QCOMPARE(loopSpy.count(), 1);
    QCOMPARE(rrSpy.count(), 1);
    QCOMPARE(loopSpy.takeFirst().at(0).toLongLong(), int64_t(100));
    QCOMPARE(rrSpy.takeFirst().at(0).toLongLong(), int64_t(100));
    QCOMPARE(m_ruler->loopStart(), int64_t(100));
    QCOMPARE(m_ruler->loopEnd(), int64_t(250));
    QCOMPARE(m_ruler->recordRegionStart(), int64_t(100));
    QCOMPARE(m_ruler->recordRegionEnd(), int64_t(250));
}

void RulerTest::plainClickOffersOnlyRemove() {
    m_ruler->setLoop(1000, 20000);
    m_ruler->setRecordRegion(2000, 30000);

    // A plain right-click (no movement) must not create, only offer removal.
    sendRightDrag(m_ruler, 50, 50);
    QCOMPARE(m_ruler->removeCalls, 1);
    QCOMPARE(m_ruler->createCalls, 0);

    QMenu* menu = m_ruler->menuRemove();
    bool hasLoopRemove = false;
    bool hasRRRemove = false;
    bool hasCreate = false;
    for (QAction* a : menu->actions()) {
        if (a->text() == "Remove Loop") hasLoopRemove = true;
        else if (a->text() == "Remove Record Region") hasRRRemove = true;
        if (a->text().startsWith("Create ")) hasCreate = true;
    }
    QVERIFY(hasLoopRemove);
    QVERIFY(hasRRRemove);
    QVERIFY(!hasCreate);

    QSignalSpy loopRemovedSpy(m_ruler, &TimelineRuler::loopRemoved);
    QSignalSpy rrRemovedSpy(m_ruler, &TimelineRuler::recordRegionRemoved);

    for (QAction* a : menu->actions())
        if (a->text() == "Remove Loop") a->trigger();
    QCOMPARE(loopRemovedSpy.count(), 1);
    QCOMPARE(m_ruler->loopStart(), int64_t(-1));
    QCOMPARE(m_ruler->recordRegionStart(), int64_t(2000)); // untouched

    for (QAction* a : menu->actions())
        if (a->text() == "Remove Record Region") a->trigger();
    QCOMPARE(rrRemovedSpy.count(), 1);
    QCOMPARE(m_ruler->recordRegionStart(), int64_t(-1));
}

void RulerTest::degenerateDragFallsBackToRemove() {
    // With snap on and a huge snap unit, both drag edges snap to sample 0, so
    // the dragged range is empty and creation is skipped: the gesture falls
    // back to the remove-only menu and leaves the existing loop untouched.
    m_ruler->setSnapToGrid(true);
    m_ruler->setLoop(500, 1000);
    sendRightDrag(m_ruler, 100, 180);

    QCOMPARE(m_ruler->createCalls, 0);
    QCOMPARE(m_ruler->removeCalls, 1);
    QCOMPARE(m_ruler->loopStart(), int64_t(500));
    QCOMPARE(m_ruler->loopEnd(), int64_t(1000));
}

QTEST_MAIN(RulerTest)

#include "test_gui_ruler.moc"
