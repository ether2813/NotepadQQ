Notepad++ plugins for Notepadqq
===============================

Notepadqq for Windows loads Notepad++ plugins (64-bit, Unicode) from this folder,
using the same layout as Notepad++ 7.6 and later:

    plugins\
        MyPlugin\
            MyPlugin.dll
            (any other files shipped with the plugin)

Plugins are also loaded from %LOCALAPPDATA%\Notepadqq\plugins\ with the same layout.
Plugin configuration files are stored in %APPDATA%\Notepadqq\plugins\Config\
(the folder returned to plugins by NPPM_GETPLUGINSCONFIGDIR).

To install a plugin downloaded for Notepad++ (for example from the Notepad++ Plugins
Admin list), extract its x64 zip file into a sub-folder named exactly like the DLL,
then restart Notepadqq. The plugin commands appear in the "Plugins" menu.

Notes:
  * Only 64-bit plugins can be loaded by the 64-bit Notepadqq build.
  * ANSI (non-Unicode) plugins are not supported, as in current Notepad++ versions.
  * See doc/notepad++-plugins.md in the Notepadqq sources for the list of supported
    Notepad++ messages and the known limitations.
