#include "include/EditorNS/nativekeyrouter.h"

#include <QAction>
#include <QApplication>
#include <QList>
#include <QWidget>

#include <windows.h>

namespace EditorNS {

namespace {

QList<HWND>& modelessDialogs()
{
    static QList<HWND> dialogs;
    return dialogs;
}

bool isScintillaWindow(HWND hwnd)
{
    wchar_t cls[32] = {0};
    if (!GetClassNameW(hwnd, cls, 32))
        return false;
    return wcscmp(cls, L"Scintilla") == 0;
}

/** Keys that Scintilla handles natively and that must never be turned into QAction shortcuts. */
bool isNativeEditingKey(int vk, bool ctrl, bool alt, bool shift)
{
    if (vk >= VK_F1 && vk <= VK_F24)
        return false;
    if (!ctrl && !alt)
        return true; // typing, navigation, Shift+navigation, Enter, Tab, Backspace, Delete...
    if (ctrl && !alt) {
        switch (vk) {
        case 'C':
        case 'X':
        case 'V':
        case 'Z':
        case 'Y':
        case 'A':
        case VK_INSERT:
        case VK_BACK:
        case VK_DELETE:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_RETURN:
            return true;
        default:
            return false;
        }
    }
    if (alt && !ctrl) {
        // Alt+Shift+arrows: rectangular selection. Alt+Backspace: undo.
        switch (vk) {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_BACK:
            return true;
        default:
            return false;
        }
    }
    Q_UNUSED(shift);
    return false;
}

QWidget* topLevelWidgetFor(HWND hwnd)
{
    HWND root = GetAncestor(hwnd, GA_ROOT);
    while (root) {
        if (QWidget* w = QWidget::find(reinterpret_cast<WId>(root)))
            return w->window();
        HWND owner = GetWindow(root, GW_OWNER);
        if (!owner)
            break;
        root = GetAncestor(owner, GA_ROOT);
    }
    return nullptr;
}

} // namespace

void NativeKeyRouter::install()
{
    static NativeKeyRouter* router = nullptr;
    if (!router) {
        router = new NativeKeyRouter();
        qApp->installNativeEventFilter(router);
    }
}

void NativeKeyRouter::addModelessDialog(void* hwnd)
{
    HWND h = static_cast<HWND>(hwnd);
    if (h && !modelessDialogs().contains(h))
        modelessDialogs().append(h);
}

void NativeKeyRouter::removeModelessDialog(void* hwnd)
{ modelessDialogs().removeAll(static_cast<HWND>(hwnd)); }

bool NativeKeyRouter::isModelessDialog(void* hwnd)
{ return modelessDialogs().contains(static_cast<HWND>(hwnd)); }

int NativeKeyRouter::qtKeyFromVirtualKey(int vk)
{
    if (vk >= 'A' && vk <= 'Z')
        return Qt::Key_A + (vk - 'A');
    if (vk >= '0' && vk <= '9')
        return Qt::Key_0 + (vk - '0');
    if (vk >= VK_F1 && vk <= VK_F24)
        return Qt::Key_F1 + (vk - VK_F1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return Qt::Key_0 + (vk - VK_NUMPAD0);
    switch (vk) {
    case VK_SPACE:
        return Qt::Key_Space;
    case VK_RETURN:
        return Qt::Key_Return;
    case VK_ESCAPE:
        return Qt::Key_Escape;
    case VK_TAB:
        return Qt::Key_Tab;
    case VK_BACK:
        return Qt::Key_Backspace;
    case VK_DELETE:
        return Qt::Key_Delete;
    case VK_INSERT:
        return Qt::Key_Insert;
    case VK_HOME:
        return Qt::Key_Home;
    case VK_END:
        return Qt::Key_End;
    case VK_PRIOR:
        return Qt::Key_PageUp;
    case VK_NEXT:
        return Qt::Key_PageDown;
    case VK_LEFT:
        return Qt::Key_Left;
    case VK_RIGHT:
        return Qt::Key_Right;
    case VK_UP:
        return Qt::Key_Up;
    case VK_DOWN:
        return Qt::Key_Down;
    case VK_ADD:
        return Qt::Key_Plus;
    case VK_SUBTRACT:
        return Qt::Key_Minus;
    case VK_MULTIPLY:
        return Qt::Key_Asterisk;
    case VK_DIVIDE:
        return Qt::Key_Slash;
    case VK_DECIMAL:
        return Qt::Key_Period;
    case VK_OEM_1:
        return Qt::Key_Semicolon;
    case VK_OEM_PLUS:
        return Qt::Key_Equal;
    case VK_OEM_COMMA:
        return Qt::Key_Comma;
    case VK_OEM_MINUS:
        return Qt::Key_Minus;
    case VK_OEM_PERIOD:
        return Qt::Key_Period;
    case VK_OEM_2:
        return Qt::Key_Slash;
    case VK_OEM_3:
        return Qt::Key_QuoteLeft;
    case VK_OEM_4:
        return Qt::Key_BracketLeft;
    case VK_OEM_5:
        return Qt::Key_Backslash;
    case VK_OEM_6:
        return Qt::Key_BracketRight;
    case VK_OEM_7:
        return Qt::Key_Apostrophe;
    default:
        return 0;
    }
}

QKeySequence NativeKeyRouter::sequenceFromVirtualKey(bool ctrl, bool alt, bool shift, int vk)
{
    const int key = qtKeyFromVirtualKey(vk);
    if (!key)
        return QKeySequence();
    Qt::KeyboardModifiers mods;
    if (ctrl)
        mods |= Qt::ControlModifier;
    if (alt)
        mods |= Qt::AltModifier;
    if (shift)
        mods |= Qt::ShiftModifier;
    if (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE)
        mods |= Qt::KeypadModifier;
    return QKeySequence(QKeyCombination(mods, static_cast<Qt::Key>(key)));
}

bool NativeKeyRouter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType != "windows_generic_MSG")
        return false;
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message < WM_KEYFIRST || msg->message > WM_KEYLAST || !msg->hwnd)
        return false;

    // 1. Plugin modeless dialogs.
    for (HWND dlg : modelessDialogs()) {
        if (!IsWindow(dlg))
            continue;
        if (dlg == msg->hwnd || IsChild(dlg, msg->hwnd)) {
            if (IsDialogMessageW(dlg, msg)) {
                *result = 0;
                return true;
            }
            break;
        }
    }

    if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN)
        return false;
    if (!isScintillaWindow(msg->hwnd))
        return false;

    const int vk = static_cast<int>(msg->wParam);
    if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN)
        return false;
    const bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;
    const bool alt = GetKeyState(VK_MENU) & 0x8000;
    const bool shift = GetKeyState(VK_SHIFT) & 0x8000;
    if (isNativeEditingKey(vk, ctrl, alt, shift))
        return false;

    QWidget* window = topLevelWidgetFor(msg->hwnd);
    if (!window)
        return false;

    // 2. Shortcuts of the window's actions (menus, toolbar and Notepad++ plugin commands).
    const QKeySequence seq = sequenceFromVirtualKey(ctrl, alt, shift, vk);
    if (!seq.isEmpty()) {
        const QList<QAction*> actions = window->findChildren<QAction*>();
        for (QAction* action : actions) {
            if (!action->isEnabled())
                continue;
            if (action->shortcuts().contains(seq)) {
                action->trigger();
                *result = 0;
                return true;
            }
        }
    }

    // 3. Alt+letter: menu mnemonics are handled by Qt on the main window.
    if (alt && !ctrl && ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))) {
        HWND mainHwnd = reinterpret_cast<HWND>(window->winId());
        SetFocus(mainHwnd);
        PostMessageW(mainHwnd, msg->message, msg->wParam, msg->lParam);
        *result = 0;
        return true;
    }
    return false;
}

} // namespace EditorNS
