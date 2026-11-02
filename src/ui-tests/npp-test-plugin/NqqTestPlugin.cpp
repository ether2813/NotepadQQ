// Minimal Notepad++ plugin used by the plugin host tests (npp-plugin-tests).
// It only uses the public Notepad++ plugin interface, exactly like a third-party plugin.

#include "Docking.h"
#include "PluginInterface.h"

#include <windows.h>

namespace {

NppData g_npp;
FuncItem g_items[3];
ShortcutKey g_insertShortcut = {true, true, false, 'T'}; // Ctrl+Alt+T
int g_flags = 0;
int g_scnModified = 0;
tTbData g_dock;
HWND g_dockWindow = nullptr;

HWND currentScintilla()
{
    int which = -1;
    SendMessageW(g_npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
    return which == 1 ? g_npp._scintillaSecondHandle : g_npp._scintillaMainHandle;
}

void insertText()
{
    const char text[] = "plugin-ok";
    SendMessageW(currentScintilla(), SCI_APPENDTEXT, sizeof(text) - 1, reinterpret_cast<LPARAM>(text));
}

void showPanel()
{
    if (!g_dockWindow) {
        g_dockWindow = CreateWindowExW(0,
            L"STATIC",
            L"Nqq test panel",
            WS_CHILD,
            0,
            0,
            200,
            150,
            g_npp._nppHandle,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        g_dock.hClient = g_dockWindow;
        g_dock.pszName = L"Nqq Test Panel";
        g_dock.dlgID = 1;
        g_dock.uMask = DWS_DF_CONT_RIGHT;
        g_dock.pszModuleName = L"NqqTestPlugin.dll";
        SendMessageW(g_npp._nppHandle, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&g_dock));
    } else {
        SendMessageW(g_npp._nppHandle, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(g_dockWindow));
    }
}

} // namespace

extern "C" __declspec(dllexport) void setInfo(NppData data)
{ g_npp = data; }

extern "C" __declspec(dllexport) const wchar_t* getName()
{ return L"Nqq Test Plugin"; }

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count)
{
    lstrcpyW(g_items[0]._itemName, L"Insert text");
    g_items[0]._pFunc = insertText;
    g_items[0]._pShKey = &g_insertShortcut;
    g_items[1]._itemName[0] = L'\0'; // separator
    g_items[1]._pFunc = nullptr;
    lstrcpyW(g_items[2]._itemName, L"Show panel");
    g_items[2]._pFunc = showPanel;
    *count = 3;
    return g_items;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* scn)
{
    if (scn->nmhdr.hwndFrom == g_npp._nppHandle) {
        if (scn->nmhdr.code == NPPN_TBMODIFICATION)
            g_flags |= 1;
        else if (scn->nmhdr.code == NPPN_READY)
            g_flags |= 2;
        else if (scn->nmhdr.code == NPPN_BUFFERACTIVATED)
            g_flags |= 4;
    } else if (scn->nmhdr.code == SCN_MODIFIED) {
        ++g_scnModified;
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT msg, WPARAM, LPARAM lParam)
{
    if (msg == NPPM_MSGTOPLUGIN) {
        auto* info = reinterpret_cast<CommunicationInfo*>(lParam);
        if (info && info->internalMsg == 42 && info->info)
            *static_cast<int*>(info->info) = 4242;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode()
{ return TRUE; }

// Test hooks (not part of the Notepad++ interface).
extern "C" __declspec(dllexport) int nqqTestFlags()
{ return g_flags; }

extern "C" __declspec(dllexport) int nqqTestScnModifiedCount()
{ return g_scnModified; }

extern "C" __declspec(dllexport) int nqqTestCommandId(int index)
{ return g_items[index]._cmdID; }
