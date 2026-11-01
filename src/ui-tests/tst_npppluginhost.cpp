// End-to-end tests of the Notepad++ plugin host (Windows only). A real plugin DLL built from
// src/ui-tests/npp-test-plugin is loaded from <build dir>/plugins by NppPluginManager.

#include "NppPlugins/npppluginmanager.h"
#include "include/EditorNS/editor.h"
#include "include/EditorNS/scintillaview.h"
#include "include/mainwindow.h"
#include "include/topeditorcontainer.h"

#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <windows.h>

class NppPluginHostTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void pluginIsLoadedWithMenuAndShortcut();
    void notificationsAreDelivered();
    void commandUsesScintillaHandle();
    void nppMessagesAreAnswered();
    void bufferMessagesMatchEditors();
    void menuCheckAndAllocation();
    void interPluginMessage();
    void dockingPanelIsHosted();

private:
    QAction* pluginAction(const QString& text) const;
    template <typename T> T pluginExport(const char* name) const
    {
        HMODULE module = GetModuleHandleW(L"NqqTestPlugin.dll");
        return module ? reinterpret_cast<T>(GetProcAddress(module, name)) : nullptr;
    }

    QTemporaryDir m_settingsDirectory;
    MainWindow* m_window = nullptr;
    HWND m_npp = nullptr;
};

void NppPluginHostTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName("Notepadqq-tests");
    QCoreApplication::setApplicationName("Notepadqq-tests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());

    m_window = new MainWindow(QStringList());
    m_window->setAttribute(Qt::WA_DeleteOnClose, false);
    m_window->resize(900, 600);
    m_window->show();
    QVERIFY(NppPluginManager::instance() != nullptr);
    m_npp = NppPluginManager::instance()->nppHandle();
    QVERIFY(m_npp != nullptr);
    // NPPN_TBMODIFICATION / NPPN_READY are sent from the event loop.
    QTest::qWait(200);
}

void NppPluginHostTest::cleanupTestCase()
{
    delete m_window;
    m_window = nullptr;
}

QAction* NppPluginHostTest::pluginAction(const QString& text) const
{
    auto* pluginsMenu = m_window->findChild<QMenu*>("menuNppPlugins");
    if (!pluginsMenu)
        return nullptr;
    for (QAction* entry : pluginsMenu->actions()) {
        if (!entry->menu() || entry->menu()->title() != QLatin1String("Nqq Test Plugin"))
            continue;
        for (QAction* a : entry->menu()->actions()) {
            if (a->text() == text)
                return a;
        }
    }
    return nullptr;
}

void NppPluginHostTest::pluginIsLoadedWithMenuAndShortcut()
{
    QVERIFY2(GetModuleHandleW(L"NqqTestPlugin.dll") != nullptr, "the test plugin was not loaded");
    QAction* insert = pluginAction("Insert text");
    QVERIFY(insert);
    QCOMPARE(insert->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T));
    QVERIFY(pluginAction("Show panel"));

    auto commandId = pluginExport<int (*)(int)>("nqqTestCommandId");
    QVERIFY(commandId);
    QCOMPARE(commandId(0), 22000); // ID_PLUGINS_CMD, as in Notepad++
}

void NppPluginHostTest::notificationsAreDelivered()
{
    auto flags = pluginExport<int (*)()>("nqqTestFlags");
    QVERIFY(flags);
    QVERIFY(flags() & 1); // NPPN_TBMODIFICATION
    QVERIFY(flags() & 2); // NPPN_READY
    QVERIFY(flags() & 4); // NPPN_BUFFERACTIVATED
}

void NppPluginHostTest::commandUsesScintillaHandle()
{
    EditorNS::Editor* editor = m_window->currentEditor();
    QVERIFY(editor);
    auto scnCount = pluginExport<int (*)()>("nqqTestScnModifiedCount");
    QVERIFY(scnCount);
    const int before = scnCount();

    // Trigger through WM_COMMAND, like a plugin or a Notepad++ menu would.
    auto commandId = pluginExport<int (*)(int)>("nqqTestCommandId");
    SendMessageW(m_npp, WM_COMMAND, MAKEWPARAM(commandId(0), 0), 0);
    QVERIFY(editor->value().endsWith("plugin-ok"));

    // SCN_MODIFIED of the main view is forwarded to beNotified.
    QVERIFY(scnCount() > before);

    // The handle given to plugins is the native view displaying the current document.
    HWND mainSci = static_cast<HWND>(m_window->topEditorContainer()->scintillaView(0)->nativeHandle());
    QCOMPARE(static_cast<sptr_t>(SendMessageW(mainSci, SCI_GETLENGTH, 0, 0)), editor->sci(SCI_GETLENGTH));
}

void NppPluginHostTest::nppMessagesAreAnswered()
{
    QCOMPARE(SendMessageW(m_npp, NPPM_GETNPPVERSION, TRUE, 0), static_cast<LRESULT>((8 << 16) | 980));
    int view = -1;
    QCOMPARE(SendMessageW(m_npp, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&view)), TRUE);
    QCOMPARE(view, 0);

    const LRESULT needed = SendMessageW(m_npp, NPPM_GETPLUGINSCONFIGDIR, 0, 0);
    QVERIFY(needed > 0);
    std::wstring dir(static_cast<size_t>(needed) + 1, L'\0');
    QCOMPARE(SendMessageW(m_npp, NPPM_GETPLUGINSCONFIGDIR, dir.size(), reinterpret_cast<LPARAM>(dir.data())), needed);
    QVERIFY(QDir(QString::fromWCharArray(dir.c_str())).exists());

    wchar_t exeDir[MAX_PATH] = {0};
    QCOMPARE(SendMessageW(m_npp, NPPM_GETNPPDIRECTORY, MAX_PATH, reinterpret_cast<LPARAM>(exeDir)), TRUE);
    QCOMPARE(QDir::fromNativeSeparators(QString::fromWCharArray(exeDir)), QCoreApplication::applicationDirPath());

    int langType = -1;
    m_window->currentEditor()->setLanguage("cpp");
    SendMessageW(m_npp, NPPM_GETCURRENTLANGTYPE, 0, reinterpret_cast<LPARAM>(&langType));
    QCOMPARE(langType, 3);                                               // L_CPP
    QCOMPARE(SendMessageW(m_npp, NPPM_SETCURRENTLANGTYPE, 0, 22), TRUE); // L_PYTHON
    QCOMPARE(m_window->currentEditor()->getLanguage()->nppName, QString("python"));
}

void NppPluginHostTest::bufferMessagesMatchEditors()
{
    EditorNS::Editor* editor = m_window->currentEditor();
    const LRESULT id = SendMessageW(m_npp, NPPM_GETCURRENTBUFFERID, 0, 0);
    QCOMPARE(static_cast<quintptr>(id), editor->bufferId());
    QCOMPARE(SendMessageW(m_npp, NPPM_GETBUFFERIDFROMPOS, 0, 0), id);
    QCOMPARE(SendMessageW(m_npp, NPPM_GETPOSFROMBUFFERID, id, 0), 0);
    QCOMPARE(SendMessageW(m_npp, NPPM_GETNBOPENFILES, 0, ALL_OPEN_FILES), 1);
    QCOMPARE(SendMessageW(m_npp, NPPM_GETBUFFERFORMAT, id, 0), static_cast<LRESULT>(editor->eolFormat()));
    QCOMPARE(SendMessageW(m_npp, NPPM_SETBUFFERFORMAT, id, 0), TRUE); // Windows (CRLF)
    QCOMPARE(editor->endOfLineSequence(), QString("\r\n"));
    QVERIFY(SendMessageW(m_npp, NPPM_GETFULLPATHFROMBUFFERID, id, 0) > 0); // "new 1"
    QCOMPARE(SendMessageW(m_npp, NPPM_GETFULLPATHFROMBUFFERID, 12345, 0), -1);
}

void NppPluginHostTest::menuCheckAndAllocation()
{
    auto commandId = pluginExport<int (*)(int)>("nqqTestCommandId");
    QCOMPARE(SendMessageW(m_npp, NPPM_SETMENUITEMCHECK, commandId(0), TRUE), TRUE);
    QVERIFY(pluginAction("Insert text")->isChecked());
    HMENU menu = reinterpret_cast<HMENU>(SendMessageW(m_npp, NPPM_GETMENUHANDLE, NPPPLUGINMENU, 0));
    QVERIFY(menu);
    QVERIFY(GetMenuState(menu, commandId(0), MF_BYCOMMAND) & MF_CHECKED);

    int start = 0;
    QCOMPARE(SendMessageW(m_npp, NPPM_ALLOCATECMDID, 4, reinterpret_cast<LPARAM>(&start)), TRUE);
    QCOMPARE(start, 23000);
    QCOMPARE(SendMessageW(m_npp, NPPM_ALLOCATEMARKER, 2, reinterpret_cast<LPARAM>(&start)), TRUE);
    QCOMPARE(start, 1);
    QCOMPARE(SendMessageW(m_npp, NPPM_ALLOCATEINDICATOR, 1, reinterpret_cast<LPARAM>(&start)), TRUE);
    QCOMPARE(start, 9);
}

void NppPluginHostTest::interPluginMessage()
{
    int reply = 0;
    CommunicationInfo info;
    info.internalMsg = 42;
    info.srcModuleName = L"tests.dll";
    info.info = &reply;
    QCOMPARE(
        SendMessageW(
            m_npp, NPPM_MSGTOPLUGIN, reinterpret_cast<WPARAM>(L"NqqTestPlugin.dll"), reinterpret_cast<LPARAM>(&info)),
        TRUE);
    QCOMPARE(reply, 4242);
    QCOMPARE(SendMessageW(
                 m_npp, NPPM_MSGTOPLUGIN, reinterpret_cast<WPARAM>(L"Missing.dll"), reinterpret_cast<LPARAM>(&info)),
        FALSE);
}

void NppPluginHostTest::dockingPanelIsHosted()
{
    QAction* show = pluginAction("Show panel");
    QVERIFY(show);
    show->trigger();
    QTest::qWait(50);
    auto* dock = m_window->findChild<QDockWidget*>("nppPlugin:NqqTestPlugin.dll:1");
    QVERIFY(dock);
    QVERIFY(dock->isVisible());
    QCOMPARE(dock->windowTitle(), QString("Nqq Test Panel"));
    const HWND client = reinterpret_cast<HWND>(
        SendMessageW(m_npp, NPPM_DMMGETPLUGINHWNDBYNAME, reinterpret_cast<WPARAM>(L"Nqq Test Panel"), 0));
    QVERIFY(client != nullptr);
    QCOMPARE(SendMessageW(m_npp, NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(client)), TRUE);
    QVERIFY(!dock->isVisible());
}

QTEST_MAIN(NppPluginHostTest)

#include "tst_npppluginhost.moc"
