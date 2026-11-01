#include "npppluginmanager.h"

#include "include/EditorNS/editor.h"
#include "include/EditorNS/nativekeyrouter.h"
#include "include/EditorNS/scintillaview.h"
#include "include/EditorNS/stylemanager.h"
#include "include/docengine.h"
#include "include/mainwindow.h"
#include "include/topeditorcontainer.h"
#include "nppdocking.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QStandardPaths>
#include <QTimer>
#include <QToolBar>
#include <QUrl>

using EditorNS::Editor;
using EditorNS::ScintillaView;

NppPluginManager* NppPluginManager::s_instance = nullptr;

namespace {

WNDPROC g_previousWndProc = nullptr;

/** Calls a plugin entry point, containing C++ exceptions thrown by the plugin. */
template <typename F> void guarded(const QString& plugin, F&& f)
{
    try {
        f();
    } catch (...) {
        qWarning() << "Notepad++ plugin" << plugin << "threw an exception";
    }
}

} // namespace

// ===================================================================== lifecycle

void NppPluginManager::attach(MainWindow* window)
{
    if (s_instance || !window)
        return;
    s_instance = new NppPluginManager(window);
}

NppPluginManager* NppPluginManager::instance()
{ return s_instance; }

NppPluginManager::NppPluginManager(MainWindow* window)
    : QObject(window)
    , m_window(window)
{
    EditorNS::NativeKeyRouter::install();

    m_hwnd = reinterpret_cast<HWND>(window->winId());
    TopEditorContainer* container = window->topEditorContainer();
    m_nppData._nppHandle = m_hwnd;
    m_nppData._scintillaMainHandle = static_cast<HWND>(container->scintillaView(0)->nativeHandle());
    m_nppData._scintillaSecondHandle = static_cast<HWND>(container->scintillaView(1)->nativeHandle());

    m_dock = new NppDockManager(window, m_hwnd, this);

    m_shadowMainMenu = CreateMenu();
    m_shadowPluginMenu = CreatePopupMenu();
    AppendMenuW(m_shadowMainMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(m_shadowPluginMenu), L"&Plugins");

    subclassWindow();
    window->installEventFilter(this);

    loadPlugins();
    buildMenus();
    connectApplicationSignals();

    // Notepad++ sends NPPN_TBMODIFICATION and then NPPN_READY once its UI is up.
    QTimer::singleShot(0, this, [this] {
        notifyPlugins(NPPN_TBMODIFICATION);
        m_ready = true;
        notifyPlugins(NPPN_READY);
        if (Editor* editor = currentEditor())
            notifyPlugins(NPPN_BUFFERACTIVATED, editor->bufferId());
        if (!m_loadErrors.isEmpty()) {
            QMessageBox::warning(m_window,
                tr("Notepad++ plugins"),
                tr("Some Notepad++ plugins could not be loaded:\n\n%1").arg(m_loadErrors.join("\n")));
        }
    });
}

NppPluginManager::~NppPluginManager()
{
    if (!m_shuttingDown)
        notifyPlugins(NPPN_SHUTDOWN);
    if (m_hwnd && g_previousWndProc)
        SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_previousWndProc));
    if (m_shadowMainMenu)
        DestroyMenu(m_shadowMainMenu); // also destroys the plugin popup
    s_instance = nullptr;
    // Plugin DLLs are intentionally not unloaded: their windows and threads may still be alive
    // until the process exits.
}

void NppPluginManager::subclassWindow()
{
    g_previousWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&NppPluginManager::windowProc)));
    m_previousWndProc = g_previousWndProc;
}

LRESULT CALLBACK NppPluginManager::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    NppPluginManager* self = s_instance;
    if (self && hwnd == self->m_hwnd) {
        LRESULT result = 0;
        if (self->handleMessage(msg, wParam, lParam, &result))
            return result;
    }
    if (g_previousWndProc)
        return CallWindowProcW(g_previousWndProc, hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool NppPluginManager::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result)
{
    bool handled = false;
    if (msg >= NPPMSG && msg < NPPMSG + 1000) {
        *result = handleNppMessage(msg, wParam, lParam, &handled);
        return handled;
    }
    if (msg >= RUNCOMMAND_USER && msg < RUNCOMMAND_USER + 100) {
        *result = handleRunCommand(msg, wParam, lParam, &handled);
        return handled;
    }
    if (msg == WM_COMMAND && lParam == 0) {
        if (handleMenuCommand(LOWORD(wParam))) {
            *result = 0;
            return true;
        }
    }
    return false;
}

QString NppPluginManager::pluginsHomePath()
{ return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins")); }

QString NppPluginManager::userPluginsPath()
{ return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Notepadqq/plugins"); }

QString NppPluginManager::pluginsConfigPath()
{
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/plugins/Config");
    QDir().mkpath(path);
    return path;
}

HWND NppPluginManager::scintillaHandle(int view) const
{ return view == 1 ? m_nppData._scintillaSecondHandle : m_nppData._scintillaMainHandle; }

// ===================================================================== loading

void NppPluginManager::loadPlugins()
{
    QStringList loadedNames;
    for (const QString& root : {pluginsHomePath(), userPluginsPath()}) {
        const QFileInfoList dirs = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& dir : dirs) {
            const QString dllName = dir.fileName() + QStringLiteral(".dll");
            const QString dll = QDir(dir.absoluteFilePath()).filePath(dllName);
            if (!QFileInfo::exists(dll))
                continue;
            if (loadedNames.contains(dllName, Qt::CaseInsensitive))
                continue;
            QString error;
            if (loadPlugin(dll, &error))
                loadedNames.append(dllName);
            else
                m_loadErrors.append(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(dll), error));
        }
    }
}

bool NppPluginManager::loadPlugin(const QString& path, QString* error)
{
    const std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
    HMODULE module = LoadLibraryExW(wpath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        const DWORD code = GetLastError();
        if (code == ERROR_BAD_EXE_FORMAT)
            *error = tr("not a 64-bit plugin");
        else
            *error = tr("cannot be loaded (error %1)").arg(code);
        return false;
    }

    auto plugin = std::make_unique<Plugin>();
    plugin->module = module;
    plugin->path = path;
    plugin->fileName = QFileInfo(path).fileName();

    using PFUNCISUNICODE = BOOL(__cdecl*)();
    auto isUnicode = reinterpret_cast<PFUNCISUNICODE>(GetProcAddress(module, "isUnicode"));
    plugin->setInfo = reinterpret_cast<PFUNCSETINFO>(GetProcAddress(module, "setInfo"));
    plugin->getName = reinterpret_cast<PFUNCGETNAME>(GetProcAddress(module, "getName"));
    plugin->getFuncsArray = reinterpret_cast<PFUNCGETFUNCSARRAY>(GetProcAddress(module, "getFuncsArray"));
    plugin->beNotified = reinterpret_cast<PBENOTIFIED>(GetProcAddress(module, "beNotified"));
    plugin->messageProc = reinterpret_cast<PMESSAGEPROC>(GetProcAddress(module, "messageProc"));

    if (!plugin->setInfo || !plugin->getName || !plugin->getFuncsArray || !plugin->beNotified || !plugin->messageProc) {
        *error = tr("not a Notepad++ plugin (missing exported functions)");
        FreeLibrary(module);
        return false;
    }
    if (!isUnicode || !isUnicode()) {
        *error = tr("ANSI plugins are not supported");
        FreeLibrary(module);
        return false;
    }

    bool ok = true;
    try {
        plugin->setInfo(m_nppData);
        const wchar_t* name = plugin->getName();
        plugin->name = name ? QString::fromWCharArray(name) : QFileInfo(path).completeBaseName();
        plugin->items = plugin->getFuncsArray(&plugin->itemCount);
    } catch (...) {
        ok = false;
    }
    if (!ok || !plugin->items || plugin->itemCount <= 0) {
        *error = tr("initialisation failed");
        FreeLibrary(module);
        return false;
    }

    m_plugins.push_back(std::move(plugin));
    return true;
}

void NppPluginManager::buildMenus()
{
    QMenuBar* bar = m_window->menuBar();
    m_pluginsMenu = new QMenu(tr("&Plugins"), m_window);
    m_pluginsMenu->setObjectName(QStringLiteral("menuNppPlugins"));

    // Insert before the "Window" menu, like Notepad++ (... Run, Plugins, Window, ?).
    QAction* before = nullptr;
    const QList<QAction*> barActions = bar->actions();
    for (int i = 0; i < barActions.size(); ++i) {
        QMenu* m = barActions[i]->menu();
        if (m && m->objectName() == QLatin1String("menu_Run") && i + 1 < barActions.size()) {
            before = barActions[i + 1];
            break;
        }
    }
    bar->insertMenu(before, m_pluginsMenu);

    for (auto& pluginPtr : m_plugins) {
        Plugin* plugin = pluginPtr.get();
        QMenu* sub = m_pluginsMenu->addMenu(plugin->name);
        plugin->menu = sub;
        HMENU shadowSub = CreatePopupMenu();
        const std::wstring wname = plugin->name.toStdWString();
        AppendMenuW(m_shadowPluginMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(shadowSub), wname.c_str());

        for (int i = 0; i < plugin->itemCount; ++i) {
            FuncItem& item = plugin->items[i];
            const int cmdId = m_nextCommandId++;
            item._cmdID = cmdId;

            Command command;
            command.plugin = plugin;
            command.index = i;

            if (!item._pFunc || item._itemName[0] == L'\0') {
                sub->addSeparator();
                AppendMenuW(shadowSub, MF_SEPARATOR, 0, nullptr);
                m_commands.insert(cmdId, command);
                continue;
            }

            const QString text = QString::fromWCharArray(item._itemName);
            QAction* action = sub->addAction(text);
            action->setObjectName(QStringLiteral("nppCmd%1").arg(cmdId));
            if (item._init2Check) {
                action->setCheckable(true);
                action->setChecked(true);
            }
            if (item._pShKey && item._pShKey->_key) {
                const QKeySequence seq = EditorNS::NativeKeyRouter::sequenceFromVirtualKey(
                    item._pShKey->_isCtrl, item._pShKey->_isAlt, item._pShKey->_isShift, item._pShKey->_key);
                if (!seq.isEmpty()) {
                    action->setShortcut(seq);
                    action->setShortcutContext(Qt::WindowShortcut);
                }
            }
            connect(action, &QAction::triggered, this, [this, cmdId] { runCommand(cmdId); });
            command.action = action;
            m_commands.insert(cmdId, command);

            AppendMenuW(shadowSub, MF_STRING | (item._init2Check ? MF_CHECKED : 0), cmdId, item._itemName);
        }
        connect(sub, &QMenu::aboutToShow, this, &NppPluginManager::syncShadowMenuState);
    }

    if (!m_plugins.empty())
        m_pluginsMenu->addSeparator();
    QAction* openFolder = m_pluginsMenu->addAction(tr("Open Plugins Folder..."));
    connect(openFolder, &QAction::triggered, this, [] {
        QDir().mkpath(pluginsHomePath());
        QDesktopServices::openUrl(QUrl::fromLocalFile(pluginsHomePath()));
    });
    QAction* about = m_pluginsMenu->addAction(tr("About Notepad++ Plugin Support..."));
    connect(about, &QAction::triggered, this, [this] {
        QStringList names;
        for (const auto& p : m_plugins)
            names.append(QStringLiteral("%1 (%2)").arg(p->name, p->fileName));
        QMessageBox::information(m_window,
            tr("Notepad++ plugins"),
            tr("Notepadqq implements the Notepad++ plugin interface (API level of Notepad++ 8.9.8).\n\n"
               "Plugin folder: %1\nUser plugin folder: %2\n\nLoaded plugins:\n%3")
                .arg(QDir::toNativeSeparators(pluginsHomePath()),
                    QDir::toNativeSeparators(userPluginsPath()),
                    names.isEmpty() ? tr("(none)") : names.join("\n")));
    });
    connect(m_pluginsMenu, &QMenu::aboutToShow, this, &NppPluginManager::syncShadowMenuState);
}

/**
 * Plugins sometimes change their menu items directly through the HMENU returned by
 * NPPM_GETMENUHANDLE (CheckMenuItem, EnableMenuItem, ModifyMenu, InsertMenu). Those menus are
 * shadow Win32 menus: their state is mirrored into the Qt menus before they are shown.
 */
void NppPluginManager::syncShadowMenuState()
{
    const int pluginCount = GetMenuItemCount(m_shadowPluginMenu);
    for (int p = 0; p < pluginCount && p < static_cast<int>(m_plugins.size()); ++p) {
        HMENU shadowSub = GetSubMenu(m_shadowPluginMenu, p);
        Plugin* plugin = m_plugins[p].get();
        if (!shadowSub || !plugin->menu)
            continue;

        QList<QAction*> dynamicActions;
        for (QAction* a : plugin->menu->actions()) {
            if (a->property("nppDynamic").toBool())
                dynamicActions.append(a);
        }
        for (QAction* a : dynamicActions) {
            plugin->menu->removeAction(a);
            a->deleteLater();
        }

        const int count = GetMenuItemCount(shadowSub);
        for (int i = 0; i < count; ++i) {
            const UINT id = GetMenuItemID(shadowSub, i);
            if (id == 0 || id == static_cast<UINT>(-1))
                continue;
            const UINT state = GetMenuState(shadowSub, i, MF_BYPOSITION);
            wchar_t text[256] = {0};
            GetMenuStringW(shadowSub, i, text, 255, MF_BYPOSITION);

            auto it = m_commands.find(static_cast<int>(id));
            if (it != m_commands.end() && it->action) {
                QAction* action = it->action;
                const bool checked = state & MF_CHECKED;
                if (checked && !action->isCheckable())
                    action->setCheckable(true);
                if (action->isCheckable())
                    action->setChecked(checked);
                action->setEnabled(!(state & (MF_GRAYED | MF_DISABLED)));
                const QString t = QString::fromWCharArray(text);
                if (!t.isEmpty() && t != action->text())
                    action->setText(t);
            } else {
                // Item added at runtime by the plugin (NPPM_ALLOCATECMDID).
                QAction* action = plugin->menu->addAction(QString::fromWCharArray(text));
                action->setProperty("nppDynamic", true);
                action->setCheckable(state & MF_CHECKED);
                action->setChecked(state & MF_CHECKED);
                action->setEnabled(!(state & (MF_GRAYED | MF_DISABLED)));
                const HWND hwnd = m_hwnd;
                connect(action, &QAction::triggered, this, [hwnd, id] {
                    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(id, 0), 0);
                });
            }
        }
    }
}

void NppPluginManager::runCommand(int cmdId)
{
    auto it = m_commands.constFind(cmdId);
    if (it == m_commands.constEnd() || !it->plugin)
        return;
    FuncItem& item = it->plugin->items[it->index];
    if (item._pFunc)
        guarded(it->plugin->name, [&] { item._pFunc(); });
}

void NppPluginManager::addToolbarIcon(int cmdId, HBITMAP bitmap, HICON icon, HICON darkIcon)
{
    auto it = m_commands.find(cmdId);
    if (it == m_commands.end() || !it->action)
        return;

    const bool dark = EditorNS::StyleManager::instance().currentTheme().isDark();
    QImage image;
    if (dark && darkIcon)
        image = QImage::fromHICON(darkIcon);
    if (image.isNull() && icon)
        image = QImage::fromHICON(icon);
    if (image.isNull() && bitmap) {
        image = QImage::fromHBITMAP(bitmap).convertToFormat(QImage::Format_ARGB32);
        // Notepad++ treats RGB(192, 192, 192) as transparent in toolbar bitmaps.
        const QRgb transparent = qRgb(192, 192, 192);
        for (int y = 0; y < image.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                if ((line[x] & 0x00FFFFFF) == (transparent & 0x00FFFFFF))
                    line[x] = 0;
            }
        }
    }
    if (image.isNull())
        return;

    QAction* action = it->action;
    action->setIcon(QIcon(QPixmap::fromImage(image)));
    if (QToolBar* toolbar = m_window->getToolBar()) {
        if (!toolbar->actions().contains(action))
            toolbar->addAction(action);
    }
}

// ===================================================================== notifications

void NppPluginManager::notifyPlugins(UINT code, quintptr idFrom)
{
    SCNotification scn;
    memset(&scn, 0, sizeof(scn));
    scn.nmhdr.hwndFrom = m_hwnd;
    scn.nmhdr.code = code;
    scn.nmhdr.idFrom = idFrom;
    for (size_t i = 0; i < m_plugins.size(); ++i) {
        Plugin* plugin = m_plugins[i].get();
        guarded(plugin->name, [&] { plugin->beNotified(&scn); });
    }
}

void NppPluginManager::connectView(ScintillaView* view)
{
    if (!view)
        return;
    m_connectedViews.append(view);
    connect(view, &ScintillaView::notify, this, [this](SCNotification* scn) {
        // Notepad++ forwards every notification of its two edit views to all plugins.
        for (size_t i = 0; i < m_plugins.size(); ++i) {
            Plugin* plugin = m_plugins[i].get();
            guarded(plugin->name, [&] { plugin->beNotified(scn); });
        }
    });
}

void NppPluginManager::connectApplicationSignals()
{
    TopEditorContainer* container = m_window->topEditorContainer();
    connectView(container->scintillaView(0));
    connectView(container->scintillaView(1));

    connect(container, &TopEditorContainer::currentEditorChanged, this, [this](EditorTabWidget*, int) {
        if (!m_ready)
            return;
        if (Editor* editor = currentEditor())
            notifyPlugins(NPPN_BUFFERACTIVATED, editor->bufferId());
    });

    auto hookEditor = [this](Editor* editor) {
        connect(editor, &Editor::currentLanguageChanged, this, [this, editor](const QString&, const QString&) {
            if (m_ready && editor == currentEditor())
                notifyPlugins(NPPN_LANGCHANGED, editor->bufferId());
        });
    };
    for (Editor* editor : Editor::allEditors())
        hookEditor(editor);
    connect(EditorNS::EditorEvents::instance(), &EditorNS::EditorEvents::editorCreated, this, hookEditor);
    connect(EditorNS::EditorEvents::instance(),
        &EditorNS::EditorEvents::editorAboutToBeDestroyed,
        this,
        [this](Editor* editor) {
            if (m_shuttingDown)
                return;
            const quintptr id = editor->bufferId();
            notifyPlugins(NPPN_FILEBEFORECLOSE, id);
            notifyPlugins(NPPN_FILECLOSED, id);
        });

    DocEngine* docEngine = m_window->getDocEngine();
    connect(
        docEngine, &DocEngine::documentLoaded, this, [this](EditorTabWidget* tw, int tab, bool wasAlreadyOpened, bool) {
            if (wasAlreadyOpened || !tw)
                return;
            if (Editor* editor = tw->editor(tab)) {
                notifyPlugins(NPPN_FILEBEFOREOPEN, editor->bufferId());
                notifyPlugins(NPPN_FILEOPENED, editor->bufferId());
            }
        });
    connect(docEngine, &DocEngine::documentAboutToBeSaved, this, [this](EditorTabWidget* tw, int tab) {
        if (Editor* editor = tw ? tw->editor(tab) : nullptr)
            notifyPlugins(NPPN_FILEBEFORESAVE, editor->bufferId());
    });
    connect(docEngine, &DocEngine::documentSaved, this, [this](EditorTabWidget* tw, int tab) {
        if (Editor* editor = tw ? tw->editor(tab) : nullptr)
            notifyPlugins(NPPN_FILESAVED, editor->bufferId());
    });

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (!m_shuttingDown) {
            m_shuttingDown = true;
            notifyPlugins(NPPN_SHUTDOWN);
        }
    });
}

bool NppPluginManager::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window) {
        if (event->type() == QEvent::WinIdChange) {
            HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
            if (hwnd != m_hwnd) {
                m_hwnd = hwnd;
                m_nppData._nppHandle = hwnd;
                subclassWindow();
            }
        } else if (event->type() == QEvent::Close) {
            notifyPlugins(NPPN_BEFORESHUTDOWN);
            QTimer::singleShot(0, this, [this] {
                if (m_window->isVisible())
                    notifyPlugins(NPPN_CANCELSHUTDOWN);
            });
        }
    }
    return QObject::eventFilter(watched, event);
}

// ===================================================================== helpers

Editor* NppPluginManager::currentEditor() const
{ return m_window->currentEditor(); }

EditorTabWidget* NppPluginManager::tabWidgetForView(int view) const
{
    TopEditorContainer* c = m_window->topEditorContainer();
    for (int i = 0; i < c->count(); ++i) {
        EditorTabWidget* tw = c->tabWidget(i);
        if (c->scintillaViewIndex(tw) == view)
            return tw;
    }
    return nullptr;
}

int NppPluginManager::currentViewIndex() const
{
    TopEditorContainer* c = m_window->topEditorContainer();
    return std::max(0, c->scintillaViewIndex(c->currentTabWidget()));
}

Editor* NppPluginManager::editorFromBufferId(quintptr id) const
{
    Editor* editor = Editor::fromBufferId(id);
    if (editor && m_window->topEditorContainer()->tabWidgetFromEditor(editor))
        return editor;
    return nullptr;
}

bool NppPluginManager::locateEditor(Editor* editor, int* view, int* index) const
{
    TopEditorContainer* c = m_window->topEditorContainer();
    EditorTabWidget* tw = editor ? c->tabWidgetFromEditor(editor) : nullptr;
    if (!tw)
        return false;
    if (view)
        *view = std::max(0, c->scintillaViewIndex(tw));
    if (index)
        *index = tw->indexOf(editor);
    return true;
}

bool NppPluginManager::activateEditor(Editor* editor)
{
    TopEditorContainer* c = m_window->topEditorContainer();
    EditorTabWidget* tw = editor ? c->tabWidgetFromEditor(editor) : nullptr;
    if (!tw)
        return false;
    tw->setCurrentIndex(tw->indexOf(editor));
    editor->setFocus();
    return true;
}

Editor* NppPluginManager::findEditorByPath(const QString& path) const
{
    const QString wanted = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    Editor* found = nullptr;
    m_window->topEditorContainer()->forEachEditor([&](int, int, EditorTabWidget*, Editor* editor) {
        const QUrl url = editor->filePath();
        if (url.isLocalFile() &&
            QDir::cleanPath(QFileInfo(url.toLocalFile()).absoluteFilePath()).compare(wanted, Qt::CaseInsensitive) ==
                0) {
            found = editor;
            return false;
        }
        return true;
    });
    return found;
}

bool NppPluginManager::openFile(const QString& path)
{
    if (Editor* editor = findEditorByPath(path))
        return activateEditor(editor);
    if (!QFileInfo::exists(path))
        return false;
    DocEngine* docEngine = m_window->getDocEngine();
    docEngine->getDocumentLoader()
        .setUrl(QUrl::fromLocalFile(path))
        .setTabWidget(m_window->topEditorContainer()->currentTabWidget())
        .execute()
        .wait();
    Editor* editor = findEditorByPath(path);
    if (editor)
        activateEditor(editor);
    return editor != nullptr;
}

bool NppPluginManager::saveEditor(Editor* editor, const QString& asPath, bool copy)
{
    TopEditorContainer* c = m_window->topEditorContainer();
    EditorTabWidget* tw = editor ? c->tabWidgetFromEditor(editor) : nullptr;
    if (!tw)
        return false;
    if (asPath.isEmpty() && editor->filePath().isEmpty()) {
        // Untitled document: Notepad++ shows the "Save As" dialog.
        activateEditor(editor);
        return triggerAction("actionSave_as");
    }
    const QUrl url = asPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(asPath);
    return m_window->getDocEngine()->saveDocument(tw, tw->indexOf(editor), url, copy) ==
           DocEngine::saveFileResult_Saved;
}

bool NppPluginManager::reloadEditor(Editor* editor, bool alert)
{
    TopEditorContainer* c = m_window->topEditorContainer();
    EditorTabWidget* tw = editor ? c->tabWidgetFromEditor(editor) : nullptr;
    if (!tw || !editor->filePath().isLocalFile())
        return false;
    m_window->getDocEngine()
        ->getDocumentLoader()
        .setUrl(editor->filePath())
        .setTabWidget(tw)
        .setReloadAction(alert ? DocEngine::ReloadActionAsk : DocEngine::ReloadActionDo)
        .execute()
        .wait();
    return true;
}

int NppPluginManager::encodingOf(Editor* editor) const
{
    if (!editor || !editor->codec())
        return -1;
    switch (editor->codec()->mibEnum()) {
    case 106: // UTF-8
        return editor->bom() ? 1 : 4;
    case 1013: // UTF-16BE
        return editor->bom() ? 2 : 6;
    case 1014: // UTF-16LE
    case 1015: // UTF-16
        return editor->bom() ? 3 : 7;
    default:
        return 0; // ANSI / other code page
    }
}

QString NppPluginManager::bufferPath(Editor* editor) const
{
    if (!editor)
        return QString();
    const QUrl url = editor->filePath();
    if (url.isLocalFile())
        return QDir::toNativeSeparators(url.toLocalFile());
    if (!url.isEmpty())
        return url.toString();
    TopEditorContainer* c = m_window->topEditorContainer();
    if (EditorTabWidget* tw = c->tabWidgetFromEditor(editor))
        return tw->tabText(editor);
    return QString();
}

QString NppPluginManager::currentWord() const
{
    Editor* editor = currentEditor();
    if (!editor)
        return QString();
    const QStringList selections = editor->dispatch("C_FUN_GET_SELECTIONS_TEXT", 0).toStringList();
    if (!selections.isEmpty() && !selections.first().isEmpty())
        return selections.first();
    return editor->dispatch("C_FUN_GET_CURRENT_WORD", 0).toString();
}

QString NppPluginManager::fileNameAtCursor() const
{
    Editor* editor = currentEditor();
    if (!editor)
        return QString();
    const sptr_t pos = editor->sci(SCI_GETCURRENTPOS);
    const sptr_t line = editor->sci(SCI_LINEFROMPOSITION, pos);
    const sptr_t start = editor->sci(SCI_POSITIONFROMLINE, line);
    const sptr_t end = editor->sci(SCI_GETLINEENDPOSITION, line);
    QByteArray text(static_cast<qsizetype>(end - start) + 1, '\0');
    Sci_TextRangeFull tr;
    tr.chrg.cpMin = start;
    tr.chrg.cpMax = end;
    tr.lpstrText = text.data();
    editor->sci(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&tr));
    text.resize(static_cast<qsizetype>(end - start));
    const qsizetype col = static_cast<qsizetype>(pos - start);
    auto isPathChar = [](char c) {
        return !(c == ' ' || c == '\t' || c == '"' || c == '\'' || c == '<' || c == '>' || c == '|' || c == '*' ||
                 c == '?' || c == '(' || c == ')');
    };
    qsizetype a = col;
    while (a > 0 && isPathChar(text[a - 1]))
        --a;
    qsizetype b = col;
    while (b < text.size() && isPathChar(text[b]))
        ++b;
    return QString::fromUtf8(text.mid(a, b - a));
}

bool NppPluginManager::triggerAction(const char* objectName)
{
    QAction* action = m_window->findChild<QAction*>(QLatin1String(objectName));
    if (!action || !action->isEnabled())
        return false;
    action->trigger();
    return true;
}
