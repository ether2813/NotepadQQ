#include "include/EditorNS/editor.h"
#include "include/Sessions/backupservice.h"
#include "include/Sessions/persistentcache.h"
#include "include/Sessions/sessions.h"
#include "include/editoruicontroller.h"
#include "include/mainwindow.h"
#include "include/notepadqq.h"
#include "include/topeditorcontainer.h"

#include <QCloseEvent>
#include <QPointer>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QtTest>

class NotepadqqTest : public QObject {
    Q_OBJECT

public:
    NotepadqqTest();

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void languagesPathIsJson();
    void acceptedCloseDisconnectsEditorUiController();
    void directWindowDeletionDestroysEditorUiControllerFirst();
    void closingWindowRestoresAllTabsAtNextStart();
    void snapshotRestoresTabsAfterShutdown();

private:
    QTemporaryDir m_settingsDirectory;
};

NotepadqqTest::NotepadqqTest() {}

void NotepadqqTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName("Notepadqq-tests");
    QCoreApplication::setApplicationName("Notepadqq-tests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

void NotepadqqTest::cleanupTestCase()
{
    EditorNS::Editor::invalidateEditorBuffer();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

void NotepadqqTest::languagesPathIsJson()
{ QVERIFY(Notepadqq::languagesPath().endsWith(".json")); }

// Returns the real editor UI controller owned directly by the window.
static EditorUiController* editorUiController(MainWindow* window)
{
    for (QObject* child : window->children()) {
        if (auto* controller = dynamic_cast<EditorUiController*>(child))
            return controller;
    }
    return nullptr;
}

// Ensures accepted close disconnects container and direct-editor callbacks before deferred deletion.
void NotepadqqTest::acceptedCloseDisconnectsEditorUiController()
{
    auto* window = new MainWindow(QStringList());
    window->setAttribute(Qt::WA_DeleteOnClose, false);

    QPointer<EditorUiController> controller = editorUiController(window);
    QVERIFY(controller);
    EditorNS::Editor* editor = window->currentEditor();
    EditorTabWidget* tabWidget = window->topEditorContainer()->currentTabWidget();
    const int tab = tabWidget->currentIndex();
    QCloseEvent closeEvent;
    QCoreApplication::sendEvent(window, &closeEvent);
    QVERIFY(closeEvent.isAccepted());
    QVERIFY(controller);

    const QString sentinelTitle("closed-window-sentinel");
    window->setWindowTitle(sentinelTitle);
    QVERIFY(QMetaObject::invokeMethod(window->topEditorContainer(),
        "currentEditorChanged",
        Qt::DirectConnection,
        Q_ARG(EditorTabWidget*, tabWidget),
        Q_ARG(int, tab)));
    QCOMPARE(window->windowTitle(), sentinelTitle);

    QVERIFY(QMetaObject::invokeMethod(editor,
        "currentLanguageChanged",
        Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("plaintext")),
        Q_ARG(QString, QStringLiteral("Plain Text"))));
    QCOMPARE(window->windowTitle(), sentinelTitle);

    delete window;
}

// Ensures direct deletion tears down the controller before MainWindow's QObject destruction phase.
void NotepadqqTest::directWindowDeletionDestroysEditorUiControllerFirst()
{
    auto* window = new MainWindow(QStringList());
    EditorUiController* controller = editorUiController(window);
    QVERIFY(controller);

    QStringList destructionOrder;
    connect(controller, &QObject::destroyed, this, [&destructionOrder] { destructionOrder.append("controller"); });
    connect(window, &QObject::destroyed, this, [&destructionOrder] { destructionOrder.append("window"); });

    delete window;

    QCOMPARE(destructionOrder, QStringList({"controller", "window"}));
}

// Notepad++ behaviour: closing the application and starting it again brings back every tab,
// including unsaved new documents and unsaved changes to files, without asking anything.
void NotepadqqTest::closingWindowRestoresAllTabsAtNextStart()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    const QString savedPath = files.filePath("saved.txt");
    const QString modifiedPath = files.filePath("modified.txt");
    for (const QString& path : {savedPath, modifiedPath}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("on disk\n");
    }

    auto* window = new MainWindow(QStringList());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    EditorTabWidget* tabs = window->topEditorContainer()->currentTabWidget();

    // 16 unsaved notes, like a typical Notepad++ scratch session.
    tabs->currentEditor()->setValue("note 0");
    for (int i = 1; i < 16; i++) {
        const int idx = tabs->addEditorTab(true, QString("new %1").arg(i + 1));
        tabs->editor(idx)->setValue(QString("note %1").arg(i));
    }
    window->getDocEngine()
        ->getDocumentLoader()
        .setUrls({QUrl::fromLocalFile(savedPath), QUrl::fromLocalFile(modifiedPath)})
        .setTabWidget(tabs)
        .execute()
        .wait();
    QTRY_COMPARE(tabs->count(), 18);
    EditorNS::Editor* modified = tabs->editor(tabs->findOpenEditorByUrl(QUrl::fromLocalFile(modifiedPath)));
    QVERIFY(modified);
    modified->setValue("changed but not saved");

    // Close through the real close path (no "save changes?" question when tabs are remembered).
    QCloseEvent closeEvent;
    QCoreApplication::sendEvent(window, &closeEvent);
    QVERIFY(closeEvent.isAccepted());
    delete window;
    QVERIFY(QFileInfo::exists(PersistentCache::cacheSessionPath()));

    // "Next start".
    auto* restored = new MainWindow(QStringList());
    restored->setAttribute(Qt::WA_DeleteOnClose, false);
    Sessions::loadSession(
        restored->getDocEngine(), restored->topEditorContainer(), PersistentCache::cacheSessionPath());
    EditorTabWidget* restoredTabs = restored->topEditorContainer()->tabWidget(0);
    QTRY_COMPARE_WITH_TIMEOUT(restoredTabs->count(), 18, 10000);

    QStringList contents;
    for (int i = 0; i < restoredTabs->count(); i++) {
        EditorNS::Editor* editor = restoredTabs->editor(i);
        QTRY_VERIFY(!editor->isLoading);
        contents << editor->value();
    }
    for (int i = 0; i < 16; i++)
        QVERIFY2(contents.contains(QString("note %1").arg(i)), qPrintable(contents.join(" | ")));
    QVERIFY(contents.contains("on disk\n"));
    QVERIFY(contents.contains("changed but not saved"));

    EditorNS::Editor* restoredModified =
        restoredTabs->editor(restoredTabs->findOpenEditorByUrl(QUrl::fromLocalFile(modifiedPath)));
    QVERIFY(restoredModified);
    QVERIFY(!restoredModified->isClean());

    delete restored;
}

// Windows shutdown/restart (or a crash) kills the process without a normal close. The periodic
// snapshot must bring every tab back at the next start.
void NotepadqqTest::snapshotRestoresTabsAfterShutdown()
{
    BackupService::clearBackupData();
    auto* window = new MainWindow(QStringList());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    EditorTabWidget* tabs = window->topEditorContainer()->currentTabWidget();
    tabs->currentEditor()->setValue("snapshot 0");
    for (int i = 1; i < 3; i++)
        tabs->editor(tabs->addEditorTab(true, QString("new %1").arg(i + 1)))->setValue(QString("snapshot %1").arg(i));

    BackupService::executeBackup();
    // Typing more after the first snapshot must be captured by the next one.
    tabs->editor(2)->setCursorPosition(0, 10);
    tabs->editor(2)->setSelectionsText({" and more"});
    BackupService::executeBackup();

    // Simulate the process being killed: no close event, no session save.
    delete window;
    QVERIFY(BackupService::detectImproperShutdown());

    QVERIFY(BackupService::restoreFromBackup());
    MainWindow* restored = MainWindow::instances().back();
    restored->setAttribute(Qt::WA_DeleteOnClose, false);
    EditorTabWidget* restoredTabs = restored->topEditorContainer()->tabWidget(0);
    QTRY_COMPARE_WITH_TIMEOUT(restoredTabs->count(), 3, 10000);
    QStringList contents;
    for (int i = 0; i < restoredTabs->count(); i++) {
        QTRY_VERIFY(!restoredTabs->editor(i)->isLoading);
        contents << restoredTabs->editor(i)->value();
    }
    QVERIFY2(contents.contains("snapshot 0"), qPrintable(contents.join(" | ")));
    QVERIFY(contents.contains("snapshot 1"));
    QVERIFY(contents.contains("snapshot 2 and more"));

    delete restored;
    BackupService::clearBackupData();
}

QTEST_MAIN(NotepadqqTest)

#include "tst_notepadqqtest.moc"
