# Notepad++ plugin support (Windows)

The Windows build of Notepadqq can load **Notepad++ plugins** without recompiling them.
Notepadqq implements the Notepad++ plugin ABI, and since 3.0 its editor is the same native
Scintilla control that Notepad++ uses (Scintilla 5.6.6 / Lexilla 5.5.3, from the Notepad++
8.9.8 source tree). From a plugin's point of view, Notepadqq looks like Notepad++ 8.9.8.

## Installing a plugin

**From the menu:** *Plugins → Install a Plugin…*, then select the plugin's `.dll` or the `.zip`
package downloaded for Notepad++ (x64). Notepadqq checks that it is a 64-bit Notepad++ plugin,
copies it into `plugins\<Name>\<Name>.dll` (or into `%LOCALAPPDATA%\Notepadqq\plugins` when
Notepadqq is installed in *Program Files*) and loads it immediately.

**By hand:** copy the plugin next to `notepadqq.exe` in any of these layouts and restart:

* `plugins\<Name>\<Name>.dll` (Notepad++ 7.6+ layout)
* `plugins\<AnyFolder>\<Name>.dll`
* `plugins\<Name>.dll`

The same layouts work in `%LOCALAPPDATA%\Notepadqq\plugins\`. DLLs are inspected without being
loaded, so dependency DLLs shipped with a plugin are not mistaken for plugins, and a 32-bit
plugin is reported with a clear message instead of being silently skipped. The commands of the
plugins appear in the **Plugins** menu, their toolbar icons in the main toolbar and their panels
as dockable windows.

*Plugins → Open Plugins Folder…* opens the folder, and
*Plugins → About Notepad++ Plugin Support…* lists the loaded plugins and the problems found.

Plugin settings are stored in `%APPDATA%\Notepadqq\plugins\Config\`
(`NPPM_GETPLUGINSCONFIGDIR`).

## How it works

| Notepad++ concept | Notepadqq implementation |
|---|---|
| `NppData._nppHandle` | The HWND of the first Notepadqq main window. It is subclassed, so `NPPM_*`, `RUNCOMMAND_USER + …` and `WM_COMMAND` messages sent with `SendMessage` are answered with Notepad++ semantics (`src/ui/NppPlugins/nppmessagehandler.cpp`). |
| `_scintillaMainHandle` / `_scintillaSecondHandle` | The two native Win32 Scintilla windows of the main window (left and right split view). As in Notepad++, these windows live as long as the main window and documents are swapped into them with `SCI_SETDOCPOINTER`, so plugins can keep the handles, call `SCI_GETDIRECTFUNCTION`, subclass the windows, etc. |
| Buffers / `BufferID` | Every tab is an `EditorNS::Editor` owning one Scintilla document. Its `BufferID` is stable for the life of the tab. |
| `beNotified` | Receives every Scintilla notification (`SCN_*`) of the two views, plus the `NPPN_*` notifications listed below. |
| Plugins menu | Built from `getFuncsArray()`. Command IDs start at 22000 as in Notepad++; shortcuts from `ShortcutKey` are honoured. |
| `NPPM_GETMENUHANDLE` | Returns a "shadow" Win32 menu that mirrors the Plugins menu. Changes made through it (`CheckMenuItem`, `EnableMenuItem`, `ModifyMenu`, `InsertMenu` with IDs from `NPPM_ALLOCATECMDID`) are reflected in the Qt menu when it opens. |
| Docking panels | `NPPM_DMMREGASDCKDLG` embeds the plugin's dialog in a Qt dock widget (`QWindow::fromWinId`). Closing / floating / docking sends `DMN_CLOSE` / `DMN_FLOAT` / `DMN_DOCK` like Notepad++. |
| Modeless dialogs | `NPPM_MODELESSDIALOG` registers the dialog with the message loop (`IsDialogMessage`), so Tab, Enter, Esc and Ctrl+C work in plugin dialogs. |
| Accelerators | Shortcuts pressed while a Scintilla window has focus are routed to the matching menu/plugin command, except the editing keys Scintilla handles itself (`src/ui/EditorNS/nativekeyrouter.cpp`). |
| Lexers | Lexilla is linked statically; `NPPM_CREATELEXER` returns lexers from it. `CreateWindow(L"Scintilla")` works in plugins because the window class is registered globally. |
| Themes | Notepad++ themes (`stylers.xml` format) are used as Notepadqq color schemes. Bundled themes live in `appdata\npp\themes`; extra Notepad++ themes can be copied to `%APPDATA%\Notepadqq\themes`. |

### Notifications sent to plugins

`NPPN_TBMODIFICATION`, `NPPN_READY`, `NPPN_BUFFERACTIVATED`, `NPPN_FILEBEFOREOPEN`,
`NPPN_FILEOPENED`, `NPPN_FILEBEFORESAVE`, `NPPN_FILESAVED`, `NPPN_FILEBEFORECLOSE`,
`NPPN_FILECLOSED`, `NPPN_LANGCHANGED`, `NPPN_BEFORESHUTDOWN`, `NPPN_CANCELSHUTDOWN`,
`NPPN_SHUTDOWN`, and all `SCN_*` notifications of the two editor views.

### Messages

Implemented: `NPPM_GETCURRENTSCINTILLA`, `GETCURRENTVIEW`, `GETCURRENTLANGTYPE`,
`SETCURRENTLANGTYPE`, `GETNBOPENFILES`, `GETOPENFILENAMES*` (deprecated variants),
`GETCURRENTDOCINDEX`, `ACTIVATEDOC`, `TRIGGERTABBARCONTEXTMENU`, `GETCURRENTBUFFERID`,
`GETBUFFERIDFROMPOS`, `GETPOSFROMBUFFERID`, `GETFULLPATHFROMBUFFERID`, `GET/SETBUFFERLANGTYPE`,
`GET/SETBUFFERENCODING`, `GET/SETBUFFERFORMAT`, `RELOADBUFFERID`, `RELOADFILE`,
`MAKECURRENTBUFFERDIRTY`, `SETUNTITLEDNAME`, `DOOPEN`, `SWITCHTOFILE`, `SAVECURRENTFILE`,
`SAVEALLFILES`, `SAVECURRENTFILEAS`, `SAVEFILE`, `LAUNCHFINDINFILESDLG`, `GETMENUHANDLE`,
`SETMENUITEMCHECK`, `MENUCOMMAND` (common `IDM_*` commands), `ADDTOOLBARICON_DEPRECATED`,
`ADDTOOLBARICON_FORDARKMODE`, `SETSTATUSBAR`, `HIDE/IS{TOOLBAR,MENU,STATUSBAR,TABBAR}`,
`SETSMOOTHFONT`, `GET/SETLINENUMBERWIDTHMODE`, `GETSHORTCUTBYCMDID`, `REMOVESHORTCUTBYCMDID`,
`ALLOCATECMDID`, `ALLOCATEMARKER`, `ALLOCATEINDICATOR`, `GETBOOKMARKID`, `MODELESSDIALOG`,
`DMMREGASDCKDLG`, `DMMSHOW`, `DMMHIDE`, `DMMUPDATEDISPINFO`, `DMMVIEWOTHERTAB`,
`DMMGETPLUGINHWNDBYNAME`, `CREATESCINTILLAHANDLE`, `ADDSCNMODIFIEDFLAGS`, `CREATELEXER`,
`GETEDITORDEFAULTFOREGROUNDCOLOR`, `GETEDITORDEFAULTBACKGROUNDCOLOR`, `ISDARKMODEENABLED`,
`GETDARKMODECOLORS`, `ISAUTOINDENTON`, `GETCURRENTMACROSTATUS`, `GETPLUGINSCONFIGDIR`,
`GETPLUGINHOMEPATH`, `GETNPPSETTINGSDIRPATH`, `GETCURRENTCMDLINE`, `GETAPPDATAPLUGINSALLOWED`,
`GETNPPVERSION`, `GETWINDOWSVERSION`, `GETLANGUAGENAME`, `GETLANGUAGEDESC`, `MSGTOPLUGIN`
(inter-plugin communication), and all `RUNCOMMAND_USER` messages (`NPPM_GETFULLCURRENTPATH`,
`GETCURRENTDIRECTORY`, `GETFILENAME`, `GETNAMEPART`, `GETEXTPART`, `GETCURRENTWORD`,
`GETNPPDIRECTORY`, `GETNPPFULLFILEPATH`, `GETFILENAMEATCURSOR`, `GETCURRENTLINE`,
`GETCURRENTCOLUMN`, `GETCURRENTLINESTR`).

Accepted but without effect (they return the documented "failure"/neutral value): session
messages (`GETNBSESSIONFILES`, `GETSESSIONFILES`, `SAVESESSION`, `SAVECURRENTSESSION`,
`LOADSESSION`), the document list (`SHOWDOCLIST`, `ISDOCLISTSHOWN`), `DARKMODESUBCLASSANDTHEME`,
external lexer auto-indent modes, `GETTABCOLORID`, `GETSETTINGSONCLOUDPATH`.

## Known limitations

Compatibility is high for plugins that use the documented plugin API (most plugins), but it
cannot be absolute, because Notepadqq is not Notepad++:

* **Only 64-bit Unicode plugins** can be loaded by the x64 build (32-bit and ANSI plugins are
  rejected with an explanation, exactly as Notepad++ x64 does).
* Plugins that reach into Notepad++'s **private windows** (its tab bar, Find dialog, document
  map, function list, or its Win32 main menu obtained with `GetMenu(nppHandle)`) cannot work,
  because those windows are Qt widgets in Notepadqq.
* Plugin commands are added to Notepadqq's shortcut handling but are not yet editable in the
  Preferences shortcut editor.
* `NPPN_FILEBEFOREOPEN` is sent right before `NPPN_FILEOPENED`, after the file has been read.
* Plugins are process-wide and are attached to the **first** Notepadqq window. Windows opened
  later work normally but are not visible to plugins.
* Notepad++ *User Defined Languages* are not loaded (the UDL lexer is built, but the UDL
  editor/XML loader is not part of Notepadqq).

If a plugin does not work, please report it with the plugin name and version.
