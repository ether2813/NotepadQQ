---
name: js-cpp-protocol
description: 'Reference for the editor command protocol ("C_CMD_*" / "C_FUN_*" messages) between the Notepadqq UI and the native Scintilla editor. Use when implementing new editor features, adding messages, or debugging editor behaviour.'
---

# Editor command protocol (native Scintilla editor)

Notepadqq historically ran CodeMirror inside Qt WebEngine and talked to it through
QWebChannel messages. The editor is now the native Scintilla component, but the message
protocol is kept so that UI code and Node.js extensions keep working.

## Where things are

| File | Role |
|------|------|
| `src/ui/include/EditorNS/editor.h`, `src/ui/EditorNS/editor.cpp` | `Editor`: one Scintilla document (buffer) per tab. `Editor::dispatch(msg, data)` implements every `C_CMD_*` / `C_FUN_*` message synchronously. |
| `src/ui/EditorNS/scintillaview.cpp` | `ScintillaView`: a Scintilla view (Win32 HWND on Windows, `ScintillaEditBase` elsewhere). One per split pane; documents are swapped in with `SCI_SETDOCPOINTER`. `ScintillaView::hidden()` is used for documents that are not displayed. |
| `src/ui/EditorNS/stylemanager.cpp` | Applies Notepad++ `langs.xml` lexers/keywords (document level) and `stylers.xml` themes (view level). |
| `src/ui-tests/tst_editor.cpp` | Tests for the protocol and the native editor. |

## Conventions

* `sendMessage(msg, data)` and `asyncSendMessageWithResult*` call `dispatch()`; promise based
  methods return already-settled promises.
* Positions exchanged with the UI are `(line, column)` where the column counts UTF-16 code
  units (CodeMirror semantics). Use `Editor::positionFromLineCol` / `lineColFromPosition` to
  convert from/to Scintilla byte positions.
* `value()` always uses `\n` line endings; the document itself keeps the real EOL sequence
  (`setEndOfLineSequence`) because Notepad++ plugins read it directly.
* Regex searches use `QRegularExpression` with JavaScript-like flags (`i`, `m`).
* Signals emitted by `Editor` (`contentChanged`, `cleanChanged`, `cursorActivity`, ...) are
  driven by Scintilla notifications and throttled with short timers.

## Adding a message

1. Add a branch to `Editor::dispatch()`.
2. Add a public `Editor` method if UI code needs it.
3. Cover it in `src/ui-tests/tst_editor.cpp`.
