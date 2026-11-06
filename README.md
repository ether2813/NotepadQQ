# <img src="https://user-images.githubusercontent.com/4319621/36906314-e3f99680-1e35-11e8-90fd-f959c9641f36.png" alt="Notepadqq" width="32" height="32" /> Notepadqq [![Build Status](https://github.com/notepadqq/notepadqq/actions/workflows/build-test.yml/badge.svg)](https://github.com/notepadqq/notepadqq/actions/workflows/build-test.yml) [![notepadqq](https://snapcraft.io/notepadqq/badge.svg)](https://snapcraft.io/notepadqq)

> [!WARNING]  
> This project is not actively maintained anymore. New maintainers are welcome.
>
> It has been reported that with the most recent OS/Qt versions, the program can crash unexpectedly. Use this at your own risk.
> 
>  -- Daniele

### Links

* [What is it?](#what-is-it)
* [Windows: native build with Notepad++ plugin support](#windows-native-build-with-notepad-plugin-support)
* [Build it yourself](#build-it-yourself)
* [Download it](#distribution-packages)

#### What is it?

Notepadqq is a text editor designed by developers, for developers. 

![screenshot_20180302_163505](https://notepadqq.com/s/images/snapshot1.png)

Please visit our [Wiki](https://github.com/notepadqq/notepadqq/wiki) for more screenshots and details.

Windows: native build with Notepad++ plugin support
-----

The editor component is now **native**: the browser-based CodeMirror editor (Qt WebEngine /
Chromium) has been replaced by [Scintilla](https://www.scintilla.org/), the same editing
component used by Notepad++. The Notepadqq user interface is unchanged.

* **Windows-native performance**: no embedded Chromium process. Every editor view is a real
  Win32 Scintilla window and documents are switched in place like in Notepad++, so opening a
  tab no longer starts a web page and large files use Scintilla's native text buffer.
* **Notepad++ plugins**: install x64 Notepad++ plugins with *Plugins → Install a Plugin…* (DLL or
  zip) or drop them into the `plugins` folder next to `notepadqq.exe`. They are loaded with their
  menus, toolbar icons, docking panels and notifications. See
  [doc/notepad++-plugins.md](doc/notepad++-plugins.md) for what is supported and the known
  limitations.
* **Sessions like Notepad++**: closing Notepadqq never asks to save; at the next start every tab
  is back, including unsaved new documents. A snapshot is taken every 7 seconds, when Notepadqq
  loses focus and when Windows shuts down or restarts, so nothing is lost after a reboot.
* **Notepad++ languages and themes**: syntax highlighting uses Notepad++'s `langs.xml` and the
  bundled Notepad++ themes (Monokai, Obsidian, Solarized, Zenburn, Dark Mode Default, ...).
  Extra Notepad++ themes can be copied to `%APPDATA%\Notepadqq\themes`.

On Linux and macOS the same editor code runs on the official Qt port of Scintilla.

### Windows build (GitHub Actions)

The [Windows Build workflow](.github/workflows/windows.yml) compiles Notepadqq with MSVC and
Qt 6.8 on every push, runs the tests, and produces two downloads:

* `notepadqq-windows-x64-installer`: `Notepadqq-<version>-x64-setup.exe`, a normal Windows
  installer (Program Files, Start menu and desktop shortcuts, optional "Edit with Notepadqq"
  context menu, uninstaller in *Apps & features*);
* `notepadqq-windows-x64-portable`: the application folder; unzip it anywhere and run
  `notepadqq.exe`.

The installer is installed, started and uninstalled on the CI runner to verify it. Pushing a
`v*` tag or creating a GitHub release attaches both zip files to the release.

### Building on Windows locally

Requirements: Visual Studio 2022 (C++ workload), CMake 3.24+, Ninja and Qt 6.8 for MSVC 2022
64-bit with the *Qt 5 Compatibility* and *Qt WebSockets* modules. From a
"x64 Native Tools Command Prompt":

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
    cmake --build build
    cmake --install build --prefix dist\Notepadqq
    C:\Qt\6.8.3\msvc2022_64\bin\windeployqt --release dist\Notepadqq\notepadqq.exe

Run `dist\Notepadqq\notepadqq.exe`. No installer is needed: the folder is portable.

Build it yourself
-----

| Build dependencies    | Dependencies      |
|-----------------------|-------------------|
| Qt 6.4 or higher      | Qt 6.4 or higher  |
| qt6-websockets-dev    | qt6-websockets    |
| qt6-svg-dev           | qt6-svg           |
| qt6-tools-dev-tools   | coreutils         |
| libuchardet-dev       | libuchardet       |
| pkg-config            |                   |


#### Get the source

    $ git clone --recursive https://github.com/notepadqq/notepadqq.git
    $ cd notepadqq

#### Build

    notepadqq$ cmake --preset release
    notepadqq$ cmake --build --preset release

To build with debug symbols, use the `dev` preset instead:

    notepadqq$ cmake --preset dev
    notepadqq$ cmake --build --preset dev

If you encounter errors make sure to have the necessary libraries installed. For Ubuntu you can do that using apt-get:

    sudo apt-get install qt6-tools-dev qt6-tools-dev-tools qt6-5compat-dev qt6-websockets-dev libqt6svg6 libqt6svg6-dev libuchardet-dev pkg-config

For CentOS:

    sudo dnf install -y qt6-qtbase-devel qt6-qttools-devel qt6-qtwebsockets-devel qt6-qtsvg-devel qt6-qtwebchannel-devel uchardet pkgconfig

Building for **macOS**? Check [here](https://github.com/notepadqq/notepadqq/wiki/Compiling-Notepadqq-on-macOS).

#### Run tests

    notepadqq$ ctest --preset release

#### Install

You can run notepadqq from its build output folder. If however you want to install it, first build it
by following the above steps, then run:

    notepadqq$ sudo cmake --install build/release

#### Qt

If the newest version of Qt isn't available on your distribution, you can use the [online installer](http://www.qt.io/download-open-source) to get the latest libraries and install them into your home directory (`$HOME/Qt`). Notepadqq will automatically use them.

Distribution Packages
---------------------

#### Ubuntu, Debian, and others:

    sudo apt install notepadqq

#### Snap

To install the latest stable version:

    sudo snap install notepadqq

You don't have the `snap` command? Follow the instructions at https://docs.snapcraft.io/core/install and then install Notepadqq as shown above.

You can follow the unstable development releases from the "edge" channel.

#### Arch Linux (community-maintained)
Notepadqq is available from Arch's [community repositories](https://www.archlinux.org/packages/community/x86_64/notepadqq/). To install using pacman:

    sudo pacman -S notepadqq

Alternatively it can be found in the AUR:

 * Development (git version): [notepadqq-git](https://aur.archlinux.org/packages/notepadqq-git/)

#### OpenSUSE (community-maintained)
Notepadqq is avilable in OpenSUSE's main repository:

     sudo zypper in notepadqq

#### Solus (community-maintained)
Notepadqq is available in the `shannon` (stable) repository:

     sudo eopkg it notepadqq

#### Others
Use a package for a compatible distribution, or build from [source](https://github.com/notepadqq/notepadqq.git).
If you want to submit a package: https://github.com/notepadqq/notepadqq-packaging
