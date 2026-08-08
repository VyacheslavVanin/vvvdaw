#include <QTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <memory>
#include "core/UndoStack.h"
#include "core/UndoCommand.h"
#include "core/TimeUtils.h"
#include "core/Settings.h"

class ValueCommand : public UndoCommand {
public:
    ValueCommand(int& value, int oldValue, int newValue)
        : m_value(value), m_old(oldValue), m_new(newValue) {}
    void execute() override { m_value = m_new; }
    void undo() override { m_value = m_old; }
    int id() const override { return 1; }
    bool mergeWith(const UndoCommand* other) override {
        auto* cmd = static_cast<const ValueCommand*>(other);
        if (&m_value != &cmd->m_value) return false;
        m_new = cmd->m_new;
        return true;
    }
    int newValue() const { return m_new; }
private:
    int& m_value;
    int m_old;
    int m_new;
};

class NoMergeCommand : public UndoCommand {
public:
    NoMergeCommand(int& value, int oldValue, int newValue)
        : m_value(value), m_old(oldValue), m_new(newValue) {}
    void execute() override { m_value = m_new; }
    void undo() override { m_value = m_old; }
    int id() const override { return 2; }
private:
    int& m_value;
    int m_old;
    int m_new;
};

class TestUndoStack : public QObject {
    Q_OBJECT
private slots:
    void executeAppliesAndRecords();
    void undoRestores();
    void redoReapplies();
    void mergeSameId();
    void noMergeWhenDisabled();
    void newCommandClearsRedo();
    void maxUndoTrim();
    void pushDoesNotExecute();
    void clear();
    void canUndoRedo();
    void snapSample();
    void formatTime();
};

void TestUndoStack::executeAppliesAndRecords() {
    int value = 0;
    UndoStack stack;
    stack.execute(std::make_unique<ValueCommand>(value, 0, 5));
    QCOMPARE(value, 5);
    QVERIFY(stack.canUndo());
}

void TestUndoStack::undoRestores() {
    int value = 0;
    UndoStack stack;
    stack.execute(std::make_unique<NoMergeCommand>(value, 0, 5));
    stack.execute(std::make_unique<NoMergeCommand>(value, 5, 10));
    QCOMPARE(value, 10);
    QVERIFY(stack.undo());
    QCOMPARE(value, 5);
    QVERIFY(stack.undo());
    QCOMPARE(value, 0);
    QVERIFY(!stack.canUndo());
}

void TestUndoStack::redoReapplies() {
    int value = 0;
    UndoStack stack;
    stack.execute(std::make_unique<ValueCommand>(value, 0, 5));
    stack.undo();
    QCOMPARE(value, 0);
    QVERIFY(stack.redo());
    QCOMPARE(value, 5);
}

void TestUndoStack::mergeSameId() {
    int value = 0;
    UndoStack stack;
    stack.execute(std::make_unique<ValueCommand>(value, 0, 5));
    stack.execute(std::make_unique<ValueCommand>(value, 5, 7));
    // Merged: second command never pushed separately.
    QCOMPARE(value, 7);
    stack.undo();
    QCOMPARE(value, 0); // single undo restores the original value
}

void TestUndoStack::noMergeWhenDisabled() {
    int value = 0;
    UndoStack stack;
    stack.execute(std::make_unique<NoMergeCommand>(value, 0, 5));
    stack.execute(std::make_unique<NoMergeCommand>(value, 5, 7));
    QCOMPARE(value, 7);
    stack.undo();
    QCOMPARE(value, 5);
    stack.undo();
    QCOMPARE(value, 0);
}

void TestUndoStack::newCommandClearsRedo() {
    int value = 0;
    UndoStack stack;
    stack.execute(std::make_unique<ValueCommand>(value, 0, 5));
    stack.undo();
    QVERIFY(stack.canRedo());
    stack.execute(std::make_unique<ValueCommand>(value, 0, 9));
    QVERIFY(!stack.canRedo());
}

void TestUndoStack::maxUndoTrim() {
    int value = 0;
    UndoStack stack;
    constexpr size_t N = UndoStack::MAX_UNDO + 10;
    for (size_t i = 0; i < N; ++i)
        stack.execute(std::make_unique<NoMergeCommand>(value, static_cast<int>(i),
                                                       static_cast<int>(i + 1)));
    QCOMPARE(value, static_cast<int>(N));
    int undos = 0;
    while (stack.canUndo() && stack.undo()) ++undos;
    QCOMPARE(undos, static_cast<int>(UndoStack::MAX_UNDO));
}

void TestUndoStack::pushDoesNotExecute() {
    int value = 0;
    UndoStack stack;
    stack.push(std::make_unique<ValueCommand>(value, 0, 5));
    QCOMPARE(value, 0); // push() does not execute
    QVERIFY(stack.undo());
    QCOMPARE(value, 0); // undoing a never-executed command must not corrupt state
    QVERIFY(stack.canRedo());
    QVERIFY(stack.redo());
    QCOMPARE(value, 5);
}

void TestUndoStack::clear() {
    int value = 0;
    UndoStack stack;
    stack.execute(std::make_unique<ValueCommand>(value, 0, 5));
    stack.execute(std::make_unique<ValueCommand>(value, 5, 7));
    stack.undo();
    stack.clear();
    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());
}

void TestUndoStack::canUndoRedo() {
    int value = 0;
    UndoStack stack;
    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());
    stack.execute(std::make_unique<ValueCommand>(value, 0, 5));
    QVERIFY(stack.canUndo());
    QVERIFY(!stack.canRedo());
    stack.undo();
    QVERIFY(!stack.canUndo());
    QVERIFY(stack.canRedo());
}

void TestUndoStack::snapSample() {
    QCOMPARE(TimeUtils::snapSample(100, 100.0), 100);
    QCOMPARE(TimeUtils::snapSample(130, 100.0), 100);
    QCOMPARE(TimeUtils::snapSample(160, 100.0), 200);
    QCOMPARE(TimeUtils::snapSample(50, 0.0), 50); // invalid unit -> passthrough
}

void TestUndoStack::formatTime() {
    QCOMPARE(TimeUtils::formatTime(0, 48000), QString("00:00:00.000"));
    QCOMPARE(TimeUtils::formatTime(48000, 48000), QString("00:00:01.000"));
    QCOMPARE(TimeUtils::formatTime(0, 0), QString("00:00:00.000"));
    QCOMPARE(TimeUtils::formatTime(60 * 48000 + 12345, 48000), QString("00:01:00.257"));
}

class TestSettings : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void recentsMostRecentFirst();
    void recentsDedupe();
    void recentsCappedAtTen();
    void recentsJsonRoundTrip();
    void removeRecentProject();
};

void TestSettings::initTestCase() {
    Settings::setConfigDirOverride("");
}

void TestSettings::cleanupTestCase() {
    Settings::setConfigDirOverride("");
}

void TestSettings::recentsMostRecentFirst() {
    Settings settings;
    settings.addRecentProject("/p/a");
    settings.addRecentProject("/p/b");
    settings.addRecentProject("/p/c");
    QCOMPARE(settings.recentProjects().size(), size_t(3));
    QCOMPARE(settings.recentProjects()[0].path, QString("/p/c"));
    QCOMPARE(settings.recentProjects()[1].path, QString("/p/b"));
    QCOMPARE(settings.recentProjects()[2].path, QString("/p/a"));

    // Touching an older entry moves it back to the front.
    settings.addRecentProject("/p/b");
    QCOMPARE(settings.recentProjects().size(), size_t(3));
    QCOMPARE(settings.recentProjects()[0].path, QString("/p/b"));
    QCOMPARE(settings.recentProjects()[1].path, QString("/p/c"));
    QCOMPARE(settings.recentProjects()[2].path, QString("/p/a"));
}

void TestSettings::recentsDedupe() {
    Settings settings;
    settings.addRecentProject("/p/a");
    settings.addRecentProject("/p/a");
    QCOMPARE(settings.recentProjects().size(), size_t(1));
    QCOMPARE(settings.recentProjects()[0].path, QString("/p/a"));
}

void TestSettings::recentsCappedAtTen() {
    Settings settings;
    for (int i = 0; i < Settings::MaxRecentProjects + 5; ++i)
        settings.addRecentProject(QString("/p/p%1").arg(i));
    QCOMPARE(settings.recentProjects().size(), size_t(Settings::MaxRecentProjects));
    QCOMPARE(settings.recentProjects().front().path,
             QString("/p/p%1").arg(Settings::MaxRecentProjects + 4));
    // Oldest entries were dropped.
    for (int i = 0; i < 5; ++i) {
        bool found = false;
        for (const auto& r : settings.recentProjects())
            if (r.path == QString("/p/p%1").arg(i)) found = true;
        QVERIFY(!found);
    }
}

void TestSettings::recentsJsonRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings::setConfigDirOverride(dir.path());

    Settings settings;
    settings.addRecentProject("/p/a");
    QTest::qWait(5);
    settings.addRecentProject("/p/b");
    QTest::qWait(5);
    settings.addRecentProject("/p/c");
    settings.save();

    Settings loaded;
    loaded.load();
    QCOMPARE(loaded.recentProjects().size(), size_t(3));
    QCOMPARE(loaded.recentProjects()[0].path, QString("/p/c"));
    QCOMPARE(loaded.recentProjects()[1].path, QString("/p/b"));
    QCOMPARE(loaded.recentProjects()[2].path, QString("/p/a"));

    Settings::setConfigDirOverride("");
}

void TestSettings::removeRecentProject() {
    Settings settings;
    settings.addRecentProject("/p/a");
    settings.addRecentProject("/p/b");
    settings.removeRecentProject("/p/missing");
    QCOMPARE(settings.recentProjects().size(), size_t(2));
    settings.removeRecentProject("/p/a");
    QCOMPARE(settings.recentProjects().size(), size_t(1));
    QCOMPARE(settings.recentProjects()[0].path, QString("/p/b"));
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    int status = 0;
    TestUndoStack undo;
    status |= QTest::qExec(&undo, argc, argv);
    TestSettings settings;
    status |= QTest::qExec(&settings, argc, argv);
    return status;
}
#include "test_core.moc"
