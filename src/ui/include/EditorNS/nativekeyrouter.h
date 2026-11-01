#ifndef NQQ_NATIVEKEYROUTER_H
#define NQQ_NATIVEKEYROUTER_H

#include <QAbstractNativeEventFilter>
#include <QKeySequence>

namespace EditorNS {

/**
 * @brief Windows message-loop integration for native child windows (Windows only).
 *
 * Qt does not see keyboard input that goes to foreign Win32 windows such as the Scintilla
 * controls and the dialogs created by Notepad++ plugins. This filter reproduces what
 * Notepad++'s own message loop does:
 *
 *  1. Messages for registered modeless dialogs (NPPM_MODELESSDIALOG) go through
 *     IsDialogMessage(), so Tab, Enter, Esc and Ctrl+C work in plugin dialogs.
 *  2. Shortcuts typed in a Scintilla window trigger the matching QAction of the window
 *     (the equivalent of Notepad++'s accelerator table), except for the editing keys that
 *     Scintilla handles natively.
 *  3. Alt+letter is forwarded to the Qt main window so menu mnemonics keep working.
 */
class NativeKeyRouter : public QAbstractNativeEventFilter {
public:
    static void install();

    static void addModelessDialog(void* hwnd);
    static void removeModelessDialog(void* hwnd);
    static bool isModelessDialog(void* hwnd);

    /** Converts a Win32 virtual-key code to a Qt key, or 0 if there is no equivalent. */
    static int qtKeyFromVirtualKey(int vk);
    /** Converts a Notepad++ ShortcutKey (ctrl/alt/shift/vk) to a QKeySequence. */
    static QKeySequence sequenceFromVirtualKey(bool ctrl, bool alt, bool shift, int vk);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
};

} // namespace EditorNS

#endif // NQQ_NATIVEKEYROUTER_H
