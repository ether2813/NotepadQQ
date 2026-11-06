#ifndef NQQ_NPPPLUGINMANAGER_H
#define NQQ_NPPPLUGINMANAGER_H

#include "npp/PluginInterface.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

#include <windows.h>

class MainWindow;
class QAction;
class QMenu;
class EditorTabWidget;
class NppDockManager;

namespace EditorNS {
class Editor;
class ScintillaView;
} // namespace EditorNS

/**
 * @brief Loads and drives Notepad++ plugins (Windows only).
 *
 * Implements the Notepad++ plugin ABI: plugin DLLs exporting setInfo/getName/getFuncsArray/
 * beNotified/messageProc/isUnicode are loaded from "plugins\<Name>\<Name>.dll" next to the
 * executable (and from %LOCALAPPDATA%\Notepadqq\plugins). Plugins receive an NppData whose
 * _nppHandle is the Notepadqq main window and whose Scintilla handles are the two native
 * Scintilla views of that window. The main window is subclassed so that NPPM_* messages,
 * RUNCOMMAND_USER messages and WM_COMMAND menu commands sent by plugins are answered with
 * Notepad++ semantics, and NPPN_* plus SCN_* notifications are delivered to beNotified().
 *
 * Plugins are process-wide: they are attached to the first Notepadqq window.
 */
class NppPluginManager : public QObject {
    Q_OBJECT
public:
    /** Creates the plugin manager for @p window if none exists yet, and loads the plugins. */
    static void attach(MainWindow* window);
    static NppPluginManager* instance();
    /**
     * Destroys the plugin manager if it is bound to @p window. Called at the very beginning of
     * MainWindow's destructor, so that plugins are shut down while the UI is still complete.
     */
    static void detach(MainWindow* window);

    ~NppPluginManager() override;

    MainWindow* window() const { return m_window; }
    HWND nppHandle() const { return m_hwnd; }
    HWND scintillaHandle(int view) const;

    /** Sends a NPPN_* notification to every plugin. */
    void notifyPlugins(UINT code, quintptr idFrom = 0);

    /** Directory containing the bundled plugins (<exe dir>\plugins). */
    /**
     * @brief Installs a plugin from a DLL or a zip package: copies it into
     *        plugins\<Name>\<Name>.dll (or the per-user plugins folder when the application
     *        folder is not writable) and loads it immediately.
     * @return an empty string on success, otherwise a message explaining the problem.
     */
    QString installPlugin(const QString& source, QString* installedName = nullptr);

    /** Paths of the loaded plugin DLLs and the problems found while loading plugins. */
    QStringList loadedPluginPaths() const;
    const QStringList& loadProblems() const { return m_loadErrors; }

    static QString pluginsHomePath();
    static QString userPluginsPath();
    static QString pluginsConfigPath();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Plugin {
        HMODULE module = nullptr;
        QString name;
        QString fileName; ///< DLL file name, e.g. "NppExec.dll"
        QString path;
        PFUNCSETINFO setInfo = nullptr;
        PFUNCGETNAME getName = nullptr;
        PFUNCGETFUNCSARRAY getFuncsArray = nullptr;
        PBENOTIFIED beNotified = nullptr;
        PMESSAGEPROC messageProc = nullptr;
        FuncItem* items = nullptr;
        int itemCount = 0;
        QPointer<QMenu> menu;
    };

    struct Command {
        Plugin* plugin = nullptr;
        int index = -1;
        QPointer<QAction> action;
    };

    explicit NppPluginManager(MainWindow* window);

    void subclassWindow();
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool handleMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result);
    LRESULT handleNppMessage(UINT msg, WPARAM wParam, LPARAM lParam, bool* handled);
    LRESULT handleRunCommand(UINT msg, WPARAM wParam, LPARAM lParam, bool* handled);
    bool handleMenuCommand(int id);

    void loadPlugins();
    QStringList discoverPlugins(const QString& root, QStringList* problems);
    bool loadPlugin(const QString& path, QString* error);
    void buildMenus();
    void addPluginMenu(Plugin* plugin);
    void installPluginInteractive();
    void connectApplicationSignals();
    void connectView(EditorNS::ScintillaView* view);
    void runCommand(int cmdId);
    void addToolbarIcon(int cmdId, HBITMAP bitmap, HICON icon, HICON darkIcon);
    void syncShadowMenuState();

    // Helpers used by the message handler.
    EditorNS::Editor* currentEditor() const;
    EditorTabWidget* tabWidgetForView(int view) const;
    int currentViewIndex() const;
    EditorNS::Editor* editorFromBufferId(quintptr id) const;
    bool locateEditor(EditorNS::Editor* editor, int* view, int* index) const;
    bool activateEditor(EditorNS::Editor* editor);
    EditorNS::Editor* findEditorByPath(const QString& path) const;
    bool openFile(const QString& path);
    bool saveEditor(EditorNS::Editor* editor, const QString& asPath = QString(), bool copy = false);
    bool reloadEditor(EditorNS::Editor* editor, bool alert);
    int encodingOf(EditorNS::Editor* editor) const;
    QString bufferPath(EditorNS::Editor* editor) const;
    QString currentWord() const;
    QString fileNameAtCursor() const;
    bool triggerAction(const char* objectName);

    static NppPluginManager* s_instance;

    MainWindow* m_window = nullptr;
    HWND m_hwnd = nullptr;
    WNDPROC m_previousWndProc = nullptr;
    NppData m_nppData;
    std::vector<std::unique_ptr<Plugin>> m_plugins;
    QHash<int, Command> m_commands;
    QStringList m_loadErrors;
    QPointer<QMenu> m_pluginsMenu;
    QAction* m_pluginsSeparator = nullptr;
    HMENU m_shadowPluginMenu = nullptr;
    HMENU m_shadowMainMenu = nullptr;
    NppDockManager* m_dock = nullptr;
    QList<QPointer<EditorNS::ScintillaView>> m_connectedViews;
    int m_nextCommandId = 22000;        // ID_PLUGINS_CMD in Notepad++
    int m_nextDynamicCommandId = 23000; // ID_PLUGINS_CMD_DYNAMIC
    int m_nextMarker = 1;               // MARKER_PLUGINS
    int m_nextIndicator = 9;            // INDICATOR_PLUGINS
    int m_lineNumberWidthMode = 0;
    bool m_ready = false;
    bool m_shuttingDown = false;
};

#endif // NQQ_NPPPLUGINMANAGER_H
