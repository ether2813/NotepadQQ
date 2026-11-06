Notepad++ plugins for Notepadqq
===============================

Notepadqq for Windows loads 64-bit (x64) Notepad++ plugins.

Easiest way: in Notepadqq, use "Plugins > Install a Plugin..." and select the plugin's
.dll file or the .zip file you downloaded. The plugin is copied to the right place and
loaded immediately, no restart needed.

Manual installation: put the plugin in this folder. Any of these layouts works:

    plugins\MyPlugin\MyPlugin.dll     (the Notepad++ layout, recommended)
    plugins\AnyFolderName\MyPlugin.dll
    plugins\MyPlugin.dll

then restart Notepadqq. The plugin commands appear in the "Plugins" menu.

When Notepadqq is installed in "Program Files", plugins installed from the menu go to
%LOCALAPPDATA%\Notepadqq\plugins\ (same layouts), which does not need administrator rights.
Plugin settings are stored in %APPDATA%\Notepadqq\plugins\Config\.

If a plugin does not appear, open "Plugins > About Notepad++ Plugin Support...": it lists the
loaded plugins and explains why a DLL could not be loaded (for example a 32-bit plugin, which
must be replaced by its 64-bit version).
