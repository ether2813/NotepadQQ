# Third-party components

| Component | Version | Origin | License |
|---|---|---|---|
| Scintilla | 5.6.6 (`scintilla/version.txt`) | Notepad++ v8.9.8.1 source tree (`scintilla/`) | `scintilla/License.txt` (HPND) |
| Lexilla | 5.5.3 (`lexilla/version.txt`) | Notepad++ v8.9.8.1 source tree (`lexilla/`), includes Notepad++'s extra lexers (ObjC, SearchResult, User Defined) | `lexilla/License.txt` (HPND) |

The Notepad++ copies are used (rather than upstream tarballs) so that plugins get exactly the
Scintilla messages, notification layouts and lexers that Notepad++ 8.9.8 provides.

Only the directories needed to build are kept (`include`, `src`, `win32`, `qt`, `lexers`,
`lexlib`). The Notepad++ plugin interface headers live in `src/ui/NppPlugins/npp/`, and the
Notepad++ `langs.model.xml`, `stylers.model.xml` and themes in `support_files/npp/`
(GPL-3.0, same license as Notepadqq).

## Local changes

* `scintilla/include/Sci_Position.h`: include `<stdint.h>` for `intptr_t` (needed by GCC/Clang).
* `lexilla/lexers/LexUser.cxx`: only include `<windows.h>` on Windows, add the standard headers
  and an `_itoa` replacement used on other platforms.
