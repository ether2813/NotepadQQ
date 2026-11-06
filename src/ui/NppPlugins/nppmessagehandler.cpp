// Implementation of the Notepad++ plugin messages (NPPM_*, RUNCOMMAND_USER and WM_COMMAND).
// See src/ui/NppPlugins/npp/Notepad_plus_msgs.h for the documentation of every message and
// doc/notepad++-plugins.md for the list of what is supported.

#include "include/EditorNS/editor.h"
#include "include/EditorNS/languageservice.h"
#include "include/EditorNS/nativekeyrouter.h"
#include "include/EditorNS/nppdefs.h"
#include "include/EditorNS/scintillaview.h"
#include "include/EditorNS/stylemanager.h"
#include "include/mainwindow.h"
#include "include/notepadqq.h"
#include "include/nqqsettings.h"
#include "include/topeditorcontainer.h"
#include "npp/Docking.h"
#include "npp/menuCmdID.h"
#include "nppdocking.h"
#include "npppluginmanager.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMenuBar>
#include <QOperatingSystemVersion>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>

#include <string>

#include <ILexer.h>
#include <Lexilla.h>

using EditorNS::Editor;

namespace {

constexpr int kDynamicCommandFirst = 23000; // ID_PLUGINS_CMD_DYNAMIC
constexpr int kDynamicCommandLast = 24999;  // ID_PLUGINS_CMD_DYNAMIC_LIMIT
constexpr int kMarkerLast = 15;             // MARKER_PLUGINS_LIMIT
constexpr int kIndicatorLast = 20;          // INDICATOR_PLUGINS_LIMIT
constexpr int kBookmarkMarker = 24;         // marker id reserved for bookmarks
constexpr int kNppVersionMajor = 8;         // Notepad++ API level implemented: 8.9.8
constexpr int kNppVersionMinorPadded = 980;
constexpr int kNppVersionMinor = 98;

/** Copies @p str to a plugin buffer of @p bufLen wchar_t. Returns false if it does not fit. */
bool copyToBuffer(const QString& str, wchar_t* buffer, size_t bufLen)
{
    if (!buffer)
        return false;
    const std::wstring w = str.toStdWString();
    if (bufLen <= w.size())
        return false;
    std::copy(w.begin(), w.end(), buffer);
    buffer[w.size()] = L'\0';
    return true;
}

/**
 * Implements the "call me twice" convention: with a null buffer, return the required length;
 * otherwise copy (if it fits) and return the copied length, or 0 on failure.
 */
LRESULT lengthOrCopy(const QString& str, wchar_t* buffer, size_t bufLen)
{
    const std::wstring w = str.toStdWString();
    if (!buffer)
        return static_cast<LRESULT>(w.size());
    return copyToBuffer(str, buffer, bufLen) ? static_cast<LRESULT>(w.size()) : 0;
}

/** Copies without a size limit (messages whose buffer size is negotiated beforehand). */
LRESULT copyUnbounded(const QString& str, wchar_t* buffer)
{
    const std::wstring w = str.toStdWString();
    if (buffer) {
        std::copy(w.begin(), w.end(), buffer);
        buffer[w.size()] = L'\0';
    }
    return static_cast<LRESULT>(w.size());
}

QString fromWide(LPARAM p)
{ return p ? QString::fromWCharArray(reinterpret_cast<const wchar_t*>(p)) : QString(); }

int langTypeOf(Editor* editor)
{
    if (!editor || !editor->getLanguage())
        return EditorNS::NppLangType::L_TEXT;
    const EditorNS::Language* lang = editor->getLanguage();
    if (lang->nppName.isEmpty())
        return EditorNS::NppLangType::L_TEXT;
    return lang->langType;
}

QString nativeLanguageName(int langType)
{
    if (const EditorNS::NppLangDef* def = EditorNS::nppLangDefByType(langType))
        return QString::fromUtf8(def->displayName);
    return QString();
}

/** Notepad++ menu command id -> object name of the equivalent Notepadqq action. */
const QHash<int, const char*>& menuCommandActions()
{
    static const QHash<int, const char*> map = {
        {IDM_FILE_NEW, "actionNew"},
        {IDM_FILE_OPEN, "actionOpen"},
        {IDM_FILE_CLOSE, "actionClose"},
        {IDM_FILE_CLOSEALL, "actionClose_All"},
        {IDM_FILE_CLOSEALL_BUT_CURRENT, "actionClose_All_BUT_Current_Document"},
        {IDM_FILE_SAVE, "actionSave"},
        {IDM_FILE_SAVEALL, "actionSave_All"},
        {IDM_FILE_SAVEAS, "actionSave_as"},
        {IDM_FILE_CLOSEALL_TOLEFT, "actionCloseLeft"},
        {IDM_FILE_CLOSEALL_TORIGHT, "actionCloseRight"},
        {IDM_FILE_PRINT, "actionPrint"},
        {IDM_FILE_PRINTNOW, "actionPrint_Now"},
        {IDM_FILE_EXIT, "actionExit"},
        {IDM_FILE_LOADSESSION, "actionLoad_Session"},
        {IDM_FILE_SAVESESSION, "actionSave_Session"},
        {IDM_FILE_RELOAD, "actionReload_from_Disk"},
        {IDM_FILE_SAVECOPYAS, "actionSave_a_Copy_As"},
        {IDM_FILE_RENAME, "actionRename"},
        {IDM_FILE_OPEN_FOLDER, "actionOpen_Folder"},
        {IDM_EDIT_BEGINENDSELECT, "actionBegin_End_Select"},
        {IDM_EDIT_TRIMTRAILING, "actionTrim_Trailing_Space"},
        {IDM_EDIT_TRIMLINEHEAD, "actionTrim_Leading_Space"},
        {IDM_EDIT_TRIM_BOTH, "actionTrim_Leading_and_Trailing_Space"},
        {IDM_EDIT_EOL2WS, "actionEOL_to_Space"},
        {IDM_EDIT_TAB2SW, "actionTAB_to_Space"},
        {IDM_EDIT_SW2TAB_LEADING, "actionSpace_to_TAB_Leading"},
        {IDM_EDIT_SW2TAB_ALL, "actionSpace_to_TAB_All"},
        {IDM_EDIT_RTL, "actionSet_RTL"},
        {IDM_EDIT_LTR, "actionSet_LTR"},
        {IDM_EDIT_FULLPATHTOCLIP, "actionCurrent_Full_File_Path_to_Clipboard"},
        {IDM_EDIT_FILENAMETOCLIP, "actionCurrent_Filename_to_Clipboard"},
        {IDM_EDIT_CURRENTDIRTOCLIP, "actionCurrent_Directory_Path_to_Clipboard"},
        {IDM_SEARCH_FIND, "actionSearch"},
        {IDM_SEARCH_FINDNEXT, "actionFind_Next"},
        {IDM_SEARCH_FINDPREV, "actionFind_Previous"},
        {IDM_SEARCH_REPLACE, "actionReplace"},
        {IDM_SEARCH_GOTOLINE, "actionGo_to_Line"},
        {IDM_SEARCH_FINDINFILES, "actionFind_in_Files"},
        {IDM_VIEW_ALL_CHARACTERS, "actionShow_All_Characters"},
        {IDM_VIEW_INDENT_GUIDE, "actionShow_Indent_Guide"},
        {IDM_VIEW_WRAP, "actionWord_wrap"},
        {IDM_VIEW_ZOOMIN, "actionZoom_In"},
        {IDM_VIEW_ZOOMOUT, "actionZoom_Out"},
        {IDM_VIEW_ZOOMRESTORE, "actionRestore_Default_Zoom"},
        {IDM_VIEW_TAB_SPACE, "actionShow_Spaces"},
        {IDM_VIEW_EOL, "actionShow_End_of_Line"},
        {IDM_VIEW_FULLSCREENTOGGLE, "actionFull_Screen"},
        {IDM_VIEW_WRAP_SYMBOL, "actionShow_Wrap_Symbol"},
        {IDM_VIEW_GOTO_ANOTHER_VIEW, "actionMove_to_Other_View"},
        {IDM_VIEW_LOAD_IN_NEW_INSTANCE, "actionOpen_in_New_Window"},
        {IDM_VIEW_IN_FIREFOX, "actionLaunch_in_Firefox"},
        {IDM_VIEW_IN_CHROME, "actionLaunch_in_Chrome"},
        {IDM_FORMAT_TODOS, "actionWindows_Format"},
        {IDM_FORMAT_TOUNIX, "actionUNIX_Format"},
        {IDM_FORMAT_TOMAC, "actionMac_Format"},
        {IDM_FORMAT_UTF_8, "actionInterpret_as_UTF_8"},
        {IDM_FORMAT_AS_UTF_8, "actionInterpret_as_UTF_8_without_BOM"},
        {IDM_FORMAT_UTF_16BE, "actionInterpret_as_UTF_16BE_UCS_2_Big_Endian"},
        {IDM_FORMAT_UTF_16LE, "actionInterpret_as_UTF_16LE_UCS_2_Little_Endian"},
        {IDM_FORMAT_CONV2_UTF_8, "actionUTF_8"},
        {IDM_FORMAT_CONV2_AS_UTF_8, "actionUTF_8_without_BOM"},
        {IDM_FORMAT_CONV2_UTF_16BE, "actionUTF_16BE"},
        {IDM_FORMAT_CONV2_UTF_16LE, "actionUTF_16LE"},
        {IDM_LANG_TEXT, "actionPlain_text"},
        {IDM_SETTING_PREFERENCE, "actionPreferences"},
    };
    return map;
}

/** Notepad++ edit commands that map directly to a Scintilla message on the current view. */
const QHash<int, unsigned int>& menuCommandScintilla()
{
    static const QHash<int, unsigned int> map = {
        {IDM_EDIT_CUT, SCI_CUT},
        {IDM_EDIT_COPY, SCI_COPY},
        {IDM_EDIT_PASTE, SCI_PASTE},
        {IDM_EDIT_UNDO, SCI_UNDO},
        {IDM_EDIT_REDO, SCI_REDO},
        {IDM_EDIT_DELETE, SCI_CLEAR},
        {IDM_EDIT_SELECTALL, SCI_SELECTALL},
        {IDM_EDIT_DUP_LINE, SCI_SELECTIONDUPLICATE},
        {IDM_EDIT_TRANSPOSE_LINE, SCI_LINETRANSPOSE},
        {IDM_EDIT_LINE_UP, SCI_MOVESELECTEDLINESUP},
        {IDM_EDIT_LINE_DOWN, SCI_MOVESELECTEDLINESDOWN},
        {IDM_EDIT_UPPERCASE, SCI_UPPERCASE},
        {IDM_EDIT_LOWERCASE, SCI_LOWERCASE},
    };
    return map;
}

} // namespace

bool NppPluginManager::handleMenuCommand(int id)
{
    if (m_commands.contains(id)) {
        runCommand(id);
        return true;
    }
    if (id >= kDynamicCommandFirst && id <= kDynamicCommandLast) {
        // Commands created with NPPM_ALLOCATECMDID are relayed to the plugins' messageProc.
        for (size_t i = 0; i < m_plugins.size(); ++i) {
            Plugin* plugin = m_plugins[i].get();
            try {
                plugin->messageProc(WM_COMMAND, static_cast<WPARAM>(id), 0);
            } catch (...) {
            }
        }
        return true;
    }

    auto sci = menuCommandScintilla().constFind(id);
    if (sci != menuCommandScintilla().constEnd()) {
        if (Editor* editor = currentEditor())
            editor->sci(sci.value());
        return true;
    }

    auto action = menuCommandActions().constFind(id);
    if (action != menuCommandActions().constEnd()) {
        triggerAction(action.value());
        return true;
    }

    TopEditorContainer* c = m_window->topEditorContainer();
    switch (id) {
    case IDM_VIEW_SWITCHTO_OTHER_VIEW:
        if (EditorTabWidget* other = c->inactiveTabWidget(false)) {
            if (Editor* editor = other->currentEditor())
                editor->setFocus();
        }
        return true;
    case IDM_VIEW_TAB_NEXT:
    case IDM_VIEW_TAB_PREV: {
        EditorTabWidget* tw = c->currentTabWidget();
        if (tw && tw->count() > 0) {
            const int delta = id == IDM_VIEW_TAB_NEXT ? 1 : -1;
            tw->setCurrentIndex((tw->currentIndex() + delta + tw->count()) % tw->count());
        }
        return true;
    }
    default:
        return false;
    }
}

LRESULT NppPluginManager::handleRunCommand(UINT msg, WPARAM wParam, LPARAM lParam, bool* handled)
{
    *handled = true;
    auto* buffer = reinterpret_cast<wchar_t*>(lParam);
    const size_t bufLen = static_cast<size_t>(wParam);
    Editor* editor = currentEditor();

    switch (msg) {
    case NPPM_GETFULLCURRENTPATH:
        return copyToBuffer(bufferPath(editor), buffer, bufLen);
    case NPPM_GETCURRENTDIRECTORY: {
        QString dir;
        if (editor && editor->filePath().isLocalFile())
            dir = QDir::toNativeSeparators(QFileInfo(editor->filePath().toLocalFile()).absolutePath());
        return copyToBuffer(dir, buffer, bufLen);
    }
    case NPPM_GETFILENAME:
        return copyToBuffer(QFileInfo(bufferPath(editor)).fileName(), buffer, bufLen);
    case NPPM_GETNAMEPART:
        return copyToBuffer(QFileInfo(bufferPath(editor)).completeBaseName(), buffer, bufLen);
    case NPPM_GETEXTPART: {
        const QString suffix = QFileInfo(bufferPath(editor)).suffix();
        return copyToBuffer(suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix, buffer, bufLen);
    }
    case NPPM_GETCURRENTWORD:
        return copyToBuffer(currentWord(), buffer, bufLen);
    case NPPM_GETNPPDIRECTORY:
        return copyToBuffer(QDir::toNativeSeparators(QCoreApplication::applicationDirPath()), buffer, bufLen);
    case NPPM_GETNPPFULLFILEPATH:
        return copyToBuffer(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()), buffer, bufLen);
    case NPPM_GETFILENAMEATCURSOR:
        return copyToBuffer(fileNameAtCursor(), buffer, bufLen);
    case NPPM_GETCURRENTLINE:
        return editor ? editor->cursorPosition().first : 0;
    case NPPM_GETCURRENTCOLUMN:
        return editor ? editor->cursorPosition().second : 0;
    case NPPM_GETCURRENTLINESTR: {
        if (!editor)
            return FALSE;
        const sptr_t line = editor->sci(SCI_LINEFROMPOSITION, editor->sci(SCI_GETCURRENTPOS));
        const sptr_t start = editor->sci(SCI_POSITIONFROMLINE, line);
        const sptr_t end = editor->sci(SCI_GETLINEENDPOSITION, line);
        QByteArray text(static_cast<qsizetype>(end - start) + 1, '\0');
        Sci_TextRangeFull tr;
        tr.chrg.cpMin = start;
        tr.chrg.cpMax = end;
        tr.lpstrText = text.data();
        editor->sci(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&tr));
        text.resize(static_cast<qsizetype>(end - start));
        return copyToBuffer(QString::fromUtf8(text), buffer, bufLen);
    }
    default:
        *handled = false;
        return 0;
    }
}

LRESULT NppPluginManager::handleNppMessage(UINT msg, WPARAM wParam, LPARAM lParam, bool* handled)
{
    *handled = true;
    TopEditorContainer* container = m_window->topEditorContainer();
    Editor* editor = currentEditor();

    switch (msg) {
    // ------------------------------------------------------------ views & buffers
    case NPPM_GETCURRENTSCINTILLA:
        if (lParam)
            *reinterpret_cast<int*>(lParam) = currentViewIndex();
        return TRUE;
    case NPPM_GETCURRENTVIEW:
        return currentViewIndex();
    case NPPM_GETCURRENTLANGTYPE:
        if (lParam)
            *reinterpret_cast<int*>(lParam) = langTypeOf(editor);
        return TRUE;
    case NPPM_SETCURRENTLANGTYPE: {
        const EditorNS::Language* lang =
            EditorNS::LanguageService::getInstance().lookupByNppLangType(static_cast<int>(lParam));
        if (!editor || !lang)
            return FALSE;
        editor->setLanguage(lang);
        return TRUE;
    }
    case NPPM_GETNBOPENFILES: {
        const int type = static_cast<int>(lParam);
        int count = 0;
        for (int view = 0; view < 2; ++view) {
            if (type == ALL_OPEN_FILES || (type == PRIMARY_VIEW && view == 0) || (type == SECOND_VIEW && view == 1)) {
                if (EditorTabWidget* tw = tabWidgetForView(view))
                    count += tw->count();
            }
        }
        return count;
    }
    case NPPM_GETOPENFILENAMES_DEPRECATED:
    case NPPM_GETOPENFILENAMESPRIMARY_DEPRECATED:
    case NPPM_GETOPENFILENAMESSECOND_DEPRECATED: {
        auto** names = reinterpret_cast<wchar_t**>(wParam);
        const int max = static_cast<int>(lParam);
        int copied = 0;
        for (int view = 0; view < 2 && names; ++view) {
            if ((msg == NPPM_GETOPENFILENAMESPRIMARY_DEPRECATED && view != 0) ||
                (msg == NPPM_GETOPENFILENAMESSECOND_DEPRECATED && view != 1))
                continue;
            EditorTabWidget* tw = tabWidgetForView(view);
            for (int i = 0; tw && i < tw->count() && copied < max; ++i)
                copyToBuffer(bufferPath(tw->editor(i)), names[copied++], MAX_PATH);
        }
        return copied;
    }
    case NPPM_GETCURRENTDOCINDEX: {
        EditorTabWidget* tw = tabWidgetForView(static_cast<int>(lParam));
        return tw ? tw->currentIndex() : -1;
    }
    case NPPM_ACTIVATEDOC: {
        EditorTabWidget* tw = tabWidgetForView(static_cast<int>(wParam));
        const int index = static_cast<int>(lParam);
        if (!tw || index < 0 || index >= tw->count())
            return FALSE;
        tw->setCurrentIndex(index);
        tw->editor(index)->setFocus();
        return TRUE;
    }
    case NPPM_TRIGGERTABBARCONTEXTMENU: {
        EditorTabWidget* tw = tabWidgetForView(static_cast<int>(wParam));
        const int index = static_cast<int>(lParam);
        if (!tw || index < 0 || index >= tw->count())
            return FALSE;
        tw->setCurrentIndex(index);
        emit tw->customContextMenuRequested(tw->tabBar()->tabRect(index).center());
        return TRUE;
    }
    case NPPM_GETCURRENTBUFFERID:
        return editor ? static_cast<LRESULT>(editor->bufferId()) : 0;
    case NPPM_GETBUFFERIDFROMPOS: {
        EditorTabWidget* tw = tabWidgetForView(static_cast<int>(lParam));
        const int index = static_cast<int>(wParam);
        if (!tw || index < 0 || index >= tw->count())
            return 0;
        return static_cast<LRESULT>(tw->editor(index)->bufferId());
    }
    case NPPM_GETPOSFROMBUFFERID: {
        int view = 0;
        int index = 0;
        if (!locateEditor(editorFromBufferId(static_cast<quintptr>(wParam)), &view, &index))
            return -1;
        return (static_cast<LRESULT>(view) << 30) | index;
    }
    case NPPM_GETFULLPATHFROMBUFFERID: {
        Editor* target = editorFromBufferId(static_cast<quintptr>(wParam));
        if (!target)
            return -1;
        return copyUnbounded(bufferPath(target), reinterpret_cast<wchar_t*>(lParam));
    }
    case NPPM_GETBUFFERLANGTYPE: {
        Editor* target = editorFromBufferId(static_cast<quintptr>(wParam));
        return target ? langTypeOf(target) : -1;
    }
    case NPPM_SETBUFFERLANGTYPE: {
        Editor* target = editorFromBufferId(static_cast<quintptr>(wParam));
        const EditorNS::Language* lang =
            EditorNS::LanguageService::getInstance().lookupByNppLangType(static_cast<int>(lParam));
        if (!target || !lang)
            return FALSE;
        target->setLanguage(lang);
        return TRUE;
    }
    case NPPM_GETBUFFERENCODING:
        return encodingOf(editorFromBufferId(static_cast<quintptr>(wParam)));
    case NPPM_SETBUFFERENCODING: {
        Editor* target = editorFromBufferId(static_cast<quintptr>(wParam));
        if (!target)
            return FALSE;
        const char* codec = nullptr;
        bool bom = false;
        switch (static_cast<int>(lParam)) {
        case 0:
            codec = "windows-1252";
            break;
        case 1:
            codec = "UTF-8";
            bom = true;
            break;
        case 2:
            codec = "UTF-16BE";
            bom = true;
            break;
        case 3:
            codec = "UTF-16LE";
            bom = true;
            break;
        case 4:
        case 5:
            codec = "UTF-8";
            break;
        case 6:
            codec = "UTF-16BE";
            break;
        case 7:
            codec = "UTF-16LE";
            break;
        default:
            return FALSE;
        }
        target->setCodec(QTextCodec::codecForName(codec));
        target->setBom(bom);
        return TRUE;
    }
    case NPPM_GETBUFFERFORMAT: {
        Editor* target = editorFromBufferId(static_cast<quintptr>(wParam));
        return target ? target->eolFormat() : -1;
    }
    case NPPM_SETBUFFERFORMAT: {
        Editor* target = editorFromBufferId(static_cast<quintptr>(wParam));
        if (!target)
            return FALSE;
        static const char* const sequences[] = {"\r\n", "\r", "\n"};
        const int format = static_cast<int>(lParam);
        if (format < 0 || format > 2)
            return FALSE;
        target->setEndOfLineSequence(QString::fromLatin1(sequences[format]));
        return TRUE;
    }
    case NPPM_RELOADBUFFERID:
        return reloadEditor(editorFromBufferId(static_cast<quintptr>(wParam)), lParam != 0);
    case NPPM_RELOADFILE:
        return reloadEditor(findEditorByPath(fromWide(lParam)), wParam != 0);
    case NPPM_MAKECURRENTBUFFERDIRTY:
        if (editor)
            editor->markDirty();
        return TRUE;
    case NPPM_SETUNTITLEDNAME: {
        Editor* target = editorFromBufferId(static_cast<quintptr>(wParam));
        if (!target || !target->filePath().isEmpty() || !lParam)
            return FALSE;
        if (EditorTabWidget* tw = container->tabWidgetFromEditor(target))
            tw->setTabText(target, fromWide(lParam));
        return TRUE;
    }
    case NPPM_GETTABCOLORID:
        return -1;

    // ------------------------------------------------------------ files
    case NPPM_DOOPEN:
        return openFile(fromWide(lParam)) ? TRUE : FALSE;
    case NPPM_SWITCHTOFILE:
        return activateEditor(findEditorByPath(fromWide(lParam))) ? TRUE : FALSE;
    case NPPM_SAVECURRENTFILE:
        return saveEditor(editor) ? TRUE : FALSE;
    case NPPM_SAVEALLFILES:
        return triggerAction("actionSave_All") ? TRUE : FALSE;
    case NPPM_SAVECURRENTFILEAS:
        return saveEditor(editor, fromWide(lParam), wParam != 0) ? TRUE : FALSE;
    case NPPM_SAVEFILE:
        return saveEditor(findEditorByPath(fromWide(lParam))) ? TRUE : FALSE;
    case NPPM_LAUNCHFINDINFILESDLG:
        return triggerAction("actionFind_in_Files") ? TRUE : FALSE;
    case NPPM_GETNBSESSIONFILES:
        if (wParam)
            *reinterpret_cast<BOOL*>(wParam) = FALSE;
        return 0;
    case NPPM_GETSESSIONFILES:
    case NPPM_LOADSESSION:
        return FALSE;
    case NPPM_SAVESESSION:
    case NPPM_SAVECURRENTSESSION:
        return 0;
    case NPPM_ENCODESCI:
    case NPPM_DECODESCI:
        return encodingOf(editor);

    // ------------------------------------------------------------ menus, toolbar, UI
    case NPPM_GETMENUHANDLE:
        return reinterpret_cast<LRESULT>(wParam == NPPMAINMENU ? m_shadowMainMenu : m_shadowPluginMenu);
    case NPPM_SETMENUITEMCHECK: {
        const int cmdId = static_cast<int>(wParam);
        auto it = m_commands.find(cmdId);
        if (it != m_commands.end() && it->action) {
            it->action->setCheckable(true);
            it->action->setChecked(lParam != 0);
        }
        CheckMenuItem(m_shadowPluginMenu, cmdId, MF_BYCOMMAND | (lParam ? MF_CHECKED : MF_UNCHECKED));
        return TRUE;
    }
    case NPPM_MENUCOMMAND:
        return handleMenuCommand(static_cast<int>(lParam)) ? TRUE : FALSE;
    case NPPM_ADDTOOLBARICON_DEPRECATED: {
        const auto* icons = reinterpret_cast<const toolbarIcons*>(lParam);
        if (icons)
            addToolbarIcon(static_cast<int>(wParam), icons->hToolbarBmp, icons->hToolbarIcon, nullptr);
        return TRUE;
    }
    case NPPM_ADDTOOLBARICON_FORDARKMODE: {
        const auto* icons = reinterpret_cast<const toolbarIconsWithDarkMode*>(lParam);
        if (icons)
            addToolbarIcon(
                static_cast<int>(wParam), icons->hToolbarBmp, icons->hToolbarIcon, icons->hToolbarIconDarkMode);
        return TRUE;
    }
    case NPPM_GETTOOLBARICONSETCHOICE:
        return 0;
    case NPPM_SETSTATUSBAR: {
        if (!lParam)
            return FALSE;
        m_window->statusBar()->showMessage(fromWide(lParam), 8000);
        return TRUE;
    }
    case NPPM_HIDETOOLBAR:
    case NPPM_ISTOOLBARHIDDEN: {
        QToolBar* toolbar = m_window->getToolBar();
        const BOOL wasHidden = toolbar ? toolbar->isHidden() : TRUE;
        if (msg == NPPM_HIDETOOLBAR && toolbar)
            toolbar->setVisible(lParam == 0);
        return wasHidden;
    }
    case NPPM_HIDEMENU:
    case NPPM_ISMENUHIDDEN: {
        const BOOL wasHidden = m_window->menuBar()->isHidden();
        if (msg == NPPM_HIDEMENU)
            m_window->menuBar()->setVisible(lParam == 0);
        return wasHidden;
    }
    case NPPM_HIDESTATUSBAR:
    case NPPM_ISSTATUSBARHIDDEN: {
        const BOOL wasHidden = m_window->statusBar()->isHidden();
        if (msg == NPPM_HIDESTATUSBAR)
            m_window->statusBar()->setVisible(lParam == 0);
        return wasHidden;
    }
    case NPPM_HIDETABBAR:
    case NPPM_ISTABBARHIDDEN: {
        EditorTabWidget* tw = container->tabWidget(0);
        const BOOL wasHidden = tw ? tw->tabBar()->isHidden() : FALSE;
        if (msg == NPPM_HIDETABBAR) {
            for (int i = 0; i < container->count(); ++i)
                container->tabWidget(i)->tabBar()->setVisible(lParam == 0);
        }
        return wasHidden;
    }
    case NPPM_SHOWDOCLIST:
    case NPPM_ISDOCLISTSHOWN:
        return FALSE;
    case NPPM_DOCLISTDISABLEEXTCOLUMN:
    case NPPM_DOCLISTDISABLEPATHCOLUMN:
    case NPPM_DISABLEAUTOUPDATE:
    case NPPM_SETEDITORBORDEREDGE:
        return TRUE;
    case NPPM_SETSMOOTHFONT:
        for (int view = 0; view < 2; ++view) {
            if (EditorNS::ScintillaView* v = container->scintillaView(view))
                v->send(SCI_SETFONTQUALITY, lParam ? SC_EFF_QUALITY_LCD_OPTIMIZED : SC_EFF_QUALITY_DEFAULT);
        }
        return TRUE;
    case NPPM_SETLINENUMBERWIDTHMODE:
        if (lParam != 0 && lParam != 1)
            return FALSE;
        m_lineNumberWidthMode = static_cast<int>(lParam);
        return TRUE;
    case NPPM_GETLINENUMBERWIDTHMODE:
        return m_lineNumberWidthMode;

    // ------------------------------------------------------------ shortcuts & IDs
    case NPPM_GETSHORTCUTBYCMDID: {
        auto it = m_commands.constFind(static_cast<int>(wParam));
        auto* sk = reinterpret_cast<ShortcutKey*>(lParam);
        if (it == m_commands.constEnd() || !sk || !it->plugin)
            return FALSE;
        const FuncItem& item = it->plugin->items[it->index];
        if (!item._pShKey || !item._pShKey->_key || (it->action && it->action->shortcut().isEmpty()))
            return FALSE;
        *sk = *item._pShKey;
        return TRUE;
    }
    case NPPM_REMOVESHORTCUTBYCMDID: {
        auto it = m_commands.find(static_cast<int>(wParam));
        if (it == m_commands.end() || !it->action)
            return FALSE;
        it->action->setShortcut(QKeySequence());
        return TRUE;
    }
    case NPPM_ALLOCATESUPPORTED_DEPRECATED:
        return TRUE;
    case NPPM_ALLOCATECMDID: {
        const int count = static_cast<int>(wParam);
        auto* start = reinterpret_cast<int*>(lParam);
        if (!start)
            return FALSE;
        if (count <= 0 || m_nextDynamicCommandId + count - 1 > kDynamicCommandLast) {
            *start = 0;
            return FALSE;
        }
        *start = m_nextDynamicCommandId;
        m_nextDynamicCommandId += count;
        return TRUE;
    }
    case NPPM_ALLOCATEMARKER: {
        const int count = static_cast<int>(wParam);
        auto* start = reinterpret_cast<int*>(lParam);
        if (!start)
            return FALSE;
        if (count <= 0 || m_nextMarker + count - 1 > kMarkerLast) {
            *start = 0;
            return FALSE;
        }
        *start = m_nextMarker;
        m_nextMarker += count;
        return TRUE;
    }
    case NPPM_ALLOCATEINDICATOR: {
        const int count = static_cast<int>(wParam);
        auto* start = reinterpret_cast<int*>(lParam);
        if (!start)
            return FALSE;
        if (count <= 0 || m_nextIndicator + count - 1 > kIndicatorLast) {
            *start = 0;
            return FALSE;
        }
        *start = m_nextIndicator;
        m_nextIndicator += count;
        return TRUE;
    }
    case NPPM_GETBOOKMARKID:
        return kBookmarkMarker;

    // ------------------------------------------------------------ dialogs & docking
    case NPPM_MODELESSDIALOG:
        if (wParam == MODELESSDIALOGADD)
            EditorNS::NativeKeyRouter::addModelessDialog(reinterpret_cast<void*>(lParam));
        else if (wParam == MODELESSDIALOGREMOVE)
            EditorNS::NativeKeyRouter::removeModelessDialog(reinterpret_cast<void*>(lParam));
        else
            return 0;
        return lParam;
    case NPPM_DMMREGASDCKDLG:
        return m_dock->registerDialog(reinterpret_cast<const void*>(lParam)) ? TRUE : FALSE;
    case NPPM_DMMSHOW:
        return m_dock->show(reinterpret_cast<HWND>(lParam)) ? TRUE : FALSE;
    case NPPM_DMMHIDE:
        return m_dock->hide(reinterpret_cast<HWND>(lParam)) ? TRUE : FALSE;
    case NPPM_DMMUPDATEDISPINFO:
        return m_dock->updateDisplayInfo(reinterpret_cast<HWND>(lParam)) ? TRUE : FALSE;
    case NPPM_DMMVIEWOTHERTAB:
        return m_dock->viewOtherTab(fromWide(lParam)) ? TRUE : FALSE;
    case NPPM_DMMGETPLUGINHWNDBYNAME:
        return reinterpret_cast<LRESULT>(m_dock->findClient(fromWide(wParam), fromWide(lParam)));
    case NPPM_DARKMODESUBCLASSANDTHEME:
        return 0;

    // ------------------------------------------------------------ Scintilla
    case NPPM_CREATESCINTILLAHANDLE: {
        HWND parent = lParam ? reinterpret_cast<HWND>(lParam) : m_hwnd;
        EditorNS::ScintillaView::hidden(); // makes sure the Scintilla window class is registered
        HWND sci = CreateWindowExW(0,
            L"Scintilla",
            L"",
            WS_CHILD | WS_VSCROLL | WS_HSCROLL | WS_CLIPCHILDREN,
            0,
            0,
            100,
            100,
            parent,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (sci)
            SendMessageW(sci, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
        return reinterpret_cast<LRESULT>(sci);
    }
    case NPPM_DESTROYSCINTILLAHANDLE_DEPRECATED:
        return TRUE;
    case NPPM_ADDSCNMODIFIEDFLAGS:
        Editor::addModEventFlags(static_cast<int>(lParam));
        return TRUE;
    case NPPM_CREATELEXER: {
        const QByteArray name = fromWide(lParam).toLatin1();
        return reinterpret_cast<LRESULT>(CreateLexer(name.constData()));
    }
    case NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR:
        return EditorNS::StyleManager::instance().currentTheme().defaultFg;
    case NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR:
        return EditorNS::StyleManager::instance().currentTheme().defaultBg;
    case NPPM_ISDARKMODEENABLED:
        return EditorNS::StyleManager::instance().currentTheme().isDark() ? TRUE : FALSE;
    case NPPM_GETDARKMODECOLORS: {
        // NppDarkMode::Colors is 12 COLORREF values.
        if (wParam != 12 * sizeof(COLORREF) || !lParam)
            return FALSE;
        const EditorNS::NppTheme& theme = EditorNS::StyleManager::instance().currentTheme();
        auto* colors = reinterpret_cast<COLORREF*>(lParam);
        const COLORREF bg = static_cast<COLORREF>(theme.defaultBg);
        const COLORREF fg = static_cast<COLORREF>(theme.defaultFg);
        const COLORREF values[12] = {bg,
            bg,
            RGB(0x45, 0x45, 0x45),
            bg,
            RGB(0xB0, 0x00, 0x00),
            fg,
            fg,
            RGB(0x80, 0x80, 0x80),
            RGB(0x40, 0x80, 0xFF),
            RGB(0x64, 0x64, 0x64),
            RGB(0x9B, 0x9B, 0x9B),
            RGB(0x48, 0x48, 0x48)};
        std::copy(values, values + 12, colors);
        return TRUE;
    }
    case NPPM_GETEXTERNALLEXERAUTOINDENTMODE:
    case NPPM_SETEXTERNALLEXERAUTOINDENTMODE:
        return FALSE;
    case NPPM_ISAUTOINDENTON:
        return NqqSettings::getInstance().General.getSmartIndentation() ? TRUE : FALSE;
    case NPPM_GETCURRENTMACROSTATUS:
        return 0; // MacroStatus::Idle
    case NPPM_GETNBUSERLANG:
        if (lParam)
            *reinterpret_cast<int*>(lParam) = 0;
        return 0;

    // ------------------------------------------------------------ paths & information
    case NPPM_GETPLUGINSCONFIGDIR:
        return lengthOrCopy(QDir::toNativeSeparators(pluginsConfigPath()),
            reinterpret_cast<wchar_t*>(lParam),
            static_cast<size_t>(wParam));
    case NPPM_GETPLUGINHOMEPATH:
        return lengthOrCopy(QDir::toNativeSeparators(pluginsHomePath()),
            reinterpret_cast<wchar_t*>(lParam),
            static_cast<size_t>(wParam));
    case NPPM_GETNPPSETTINGSDIRPATH:
        return lengthOrCopy(
            QDir::toNativeSeparators(Notepadqq::userConfigPath()),
            reinterpret_cast<wchar_t*>(lParam),
            static_cast<size_t>(wParam));
    case NPPM_GETSETTINGSONCLOUDPATH:
        if (lParam && wParam > 0)
            reinterpret_cast<wchar_t*>(lParam)[0] = L'\0';
        return 0;
    case NPPM_GETCURRENTCMDLINE:
        return lengthOrCopy(QCoreApplication::arguments().mid(1).join(QLatin1Char(' ')),
            reinterpret_cast<wchar_t*>(lParam),
            static_cast<size_t>(wParam));
    case NPPM_GETNATIVELANGFILENAME:
        return 0;
    case NPPM_GETAPPDATAPLUGINSALLOWED:
        return TRUE;
    case NPPM_GETNPPVERSION:
        return (kNppVersionMajor << 16) | (wParam ? kNppVersionMinorPadded : kNppVersionMinor);
    case NPPM_GETWINDOWSVERSION: {
        const auto current = QOperatingSystemVersion::current();
        if (current >= QOperatingSystemVersion::Windows11)
            return WV_WIN11;
        if (current >= QOperatingSystemVersion::Windows10)
            return WV_WIN10;
        return WV_WIN81;
    }
    case NPPM_GETCURRENTNATIVELANGENCODING:
        return CP_UTF8;
    case NPPM_GETLANGUAGENAME:
    case NPPM_GETLANGUAGEDESC:
        return copyUnbounded(nativeLanguageName(static_cast<int>(wParam)), reinterpret_cast<wchar_t*>(lParam));
    case NPPM_GETENABLETHEMETEXTUREFUNC_DEPRECATED: {
        HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
        if (!uxtheme)
            uxtheme = LoadLibraryW(L"uxtheme.dll");
        return uxtheme ? reinterpret_cast<LRESULT>(GetProcAddress(uxtheme, "EnableThemeDialogTexture")) : 0;
    }

    // ------------------------------------------------------------ inter-plugin communication
    case NPPM_MSGTOPLUGIN: {
        const QString dest = fromWide(wParam);
        for (size_t i = 0; i < m_plugins.size(); ++i) {
            Plugin* plugin = m_plugins[i].get();
            if (plugin->fileName.compare(dest, Qt::CaseInsensitive) == 0) {
                try {
                    plugin->messageProc(NPPM_MSGTOPLUGIN, wParam, lParam);
                } catch (...) {
                }
                return TRUE;
            }
        }
        return FALSE;
    }

    default:
        *handled = false;
        return 0;
    }
}
