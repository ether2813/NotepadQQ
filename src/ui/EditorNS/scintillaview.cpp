#include "include/EditorNS/scintillaview.h"

#include <QApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QPlatformSurfaceEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <ScintillaEditBase.h>
#endif

namespace EditorNS {

QByteArray SciSender::getStr(unsigned int msg, uptr_t wParam) const
{
    const sptr_t len = send(msg, wParam, 0);
    if (len <= 0)
        return QByteArray();
    QByteArray buf(static_cast<qsizetype>(len) + 1, '\0');
    send(msg, wParam, reinterpret_cast<sptr_t>(buf.data()));
    buf.resize(static_cast<qsizetype>(len));
    return buf;
}

namespace {

QList<ScintillaView*>& viewRegistry()
{
    static QList<ScintillaView*> views;
    return views;
}

HiddenScintillaNotifier* notifierInstance()
{
    static HiddenScintillaNotifier* notifier = new HiddenScintillaNotifier(qApp);
    return notifier;
}

QPointer<QWidget>& parkingStorage()
{
    static QPointer<QWidget> parking;
    return parking;
}

} // namespace

QWidget* ScintillaView::parkingWidget()
{
    QPointer<QWidget>& parking = parkingStorage();
    if (!parking) {
        parking = new QWidget(nullptr, Qt::Tool);
        parking->setObjectName(QStringLiteral("nqqScintillaParking"));
        parking->setAttribute(Qt::WA_DontShowOnScreen);
        parking->resize(16, 16);
        qAddPostRoutine([] { delete parkingStorage().data(); });
    }
    return parking;
}

QObject* ScintillaView::hiddenNotifier()
{ return notifierInstance(); }

const QList<ScintillaView*>& ScintillaView::allViews()
{ return viewRegistry(); }

ScintillaView* ScintillaView::fromNativeHandle(void* handle)
{
    for (ScintillaView* view : viewRegistry()) {
        if (view->nativeHandle() == handle)
            return view;
    }
    return nullptr;
}

#ifdef Q_OS_WIN
// ===================================================================== Win32

namespace {

using SciDirectFn = sptr_t (*)(sptr_t, unsigned int, uptr_t, sptr_t);

HINSTANCE appInstance()
{ return GetModuleHandleW(nullptr); }

void ensureScintillaRegistered()
{
    static const bool registered = [] {
        Scintilla_RegisterClasses(appInstance());
        return true;
    }();
    Q_UNUSED(registered);
}

HWND g_hiddenHwnd = nullptr;

LRESULT CALLBACK sinkWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NOTIFY && lParam) {
        auto* hdr = reinterpret_cast<NMHDR*>(lParam);
        // Notifications of parked views are dropped: they are not displayed anywhere.
        if (hdr->hwndFrom == g_hiddenHwnd && g_hiddenHwnd)
            emit notifierInstance() -> notify(reinterpret_cast<SCNotification*>(lParam));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/** Hidden top-level window: parent of the hidden Scintilla and of parked views' HWNDs. */
HWND sinkWindow()
{
    static HWND sink = [] {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = sinkWindowProc;
        wc.hInstance = appInstance();
        wc.lpszClassName = L"NotepadqqScintillaSink";
        RegisterClassW(&wc);
        return CreateWindowExW(WS_EX_TOOLWINDOW,
            L"NotepadqqScintillaSink",
            L"",
            WS_POPUP,
            0,
            0,
            0,
            0,
            nullptr,
            nullptr,
            appInstance(),
            nullptr);
    }();
    return sink;
}

class HiddenScintilla final : public SciSender {
public:
    HiddenScintilla()
    {
        ensureScintillaRegistered();
        m_hwnd = CreateWindowExW(
            0, L"Scintilla", L"", WS_CHILD, 0, 0, 640, 480, sinkWindow(), nullptr, appInstance(), nullptr);
        g_hiddenHwnd = m_hwnd;
        m_fn = reinterpret_cast<SciDirectFn>(SendMessageW(m_hwnd, SCI_GETDIRECTFUNCTION, 0, 0));
        m_ptr = static_cast<sptr_t>(SendMessageW(m_hwnd, SCI_GETDIRECTPOINTER, 0, 0));
        send(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT | SC_PERFORMED_UNDO | SC_PERFORMED_REDO);
        send(SCI_SETUNDOCOLLECTION, 1);
    }

    sptr_t send(unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0) const override
    {
        if (m_fn)
            return m_fn(m_ptr, msg, wParam, lParam);
        return static_cast<sptr_t>(SendMessageW(m_hwnd, msg, wParam, lParam));
    }

private:
    HWND m_hwnd = nullptr;
    SciDirectFn m_fn = nullptr;
    sptr_t m_ptr = 0;
};

} // namespace

SciSender& ScintillaView::hidden()
{
    static HiddenScintilla* instance = new HiddenScintilla();
    return *instance;
}

ScintillaView::ScintillaView(QWidget* parent)
    : QWidget(parent ? parent : parkingWidget())
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);

    ensureScintillaRegistered();
    HWND host = reinterpret_cast<HWND>(winId());
    HWND sci = CreateWindowExW(0,
        L"Scintilla",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0,
        0,
        100,
        100,
        host,
        nullptr,
        appInstance(),
        nullptr);
    m_hwnd = sci;
    m_directFn = reinterpret_cast<SciDirectFn>(SendMessageW(sci, SCI_GETDIRECTFUNCTION, 0, 0));
    m_directPtr = static_cast<sptr_t>(SendMessageW(sci, SCI_GETDIRECTPOINTER, 0, 0));

    viewRegistry().append(this);
    syncGeometry();
    hookHostWindow();
}

ScintillaView::~ScintillaView()
{
    viewRegistry().removeAll(this);
    if (m_hwnd) {
        DestroyWindow(static_cast<HWND>(m_hwnd));
        m_hwnd = nullptr;
    }
}

sptr_t ScintillaView::send(unsigned int msg, uptr_t wParam, sptr_t lParam) const
{
    // The direct function must only be used from the thread owning the window. Plugins
    // running worker threads use SendMessage on the HWND, which Windows marshals for us.
    if (m_directFn)
        return m_directFn(m_directPtr, msg, wParam, lParam);
    return static_cast<sptr_t>(SendMessageW(static_cast<HWND>(m_hwnd), msg, wParam, lParam));
}

void* ScintillaView::nativeHandle() const
{ return m_hwnd; }

void ScintillaView::focusEditor()
{
    QWidget::setFocus(Qt::OtherFocusReason);
    if (GetFocus() != static_cast<HWND>(m_hwnd))
        SetFocus(static_cast<HWND>(m_hwnd));
}

bool ScintillaView::editorHasFocus() const
{ return GetFocus() == static_cast<HWND>(m_hwnd); }

void ScintillaView::syncGeometry()
{
    if (!m_hwnd || m_parked)
        return;
    RECT rc;
    GetClientRect(reinterpret_cast<HWND>(winId()), &rc);
    SetWindowPos(static_cast<HWND>(m_hwnd),
        nullptr,
        0,
        0,
        rc.right - rc.left,
        rc.bottom - rc.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

void ScintillaView::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    syncGeometry();
}

void ScintillaView::focusInEvent(QFocusEvent* ev)
{
    QWidget::focusInEvent(ev);
    if (m_hwnd && GetFocus() != static_cast<HWND>(m_hwnd))
        SetFocus(static_cast<HWND>(m_hwnd));
}

void ScintillaView::park()
{
    if (!m_hwnd || m_parked)
        return;
    ShowWindow(static_cast<HWND>(m_hwnd), SW_HIDE);
    SetParent(static_cast<HWND>(m_hwnd), sinkWindow());
    m_parked = true;
}

void ScintillaView::unpark()
{
    if (!m_hwnd || !testAttribute(Qt::WA_WState_Created))
        return;
    HWND host = reinterpret_cast<HWND>(winId());
    if (!host)
        return;
    if (GetParent(static_cast<HWND>(m_hwnd)) != host)
        SetParent(static_cast<HWND>(m_hwnd), host);
    m_parked = false;
    ShowWindow(static_cast<HWND>(m_hwnd), SW_SHOWNA);
    syncGeometry();
    hookHostWindow();
}

/**
 * Qt may destroy and re-create the host's native window at any time (reparenting, a sibling
 * becoming native, ...). Destroying it would destroy the Scintilla child window too, and with it
 * the handle given to Notepad++ plugins. The host QWindow announces the destruction of its
 * native surface, which is when the Scintilla window is moved to a safe parking window.
 */
void ScintillaView::hookHostWindow()
{
    QWindow* window = windowHandle();
    if (window == m_hostWindow)
        return;
    if (m_hostWindow)
        m_hostWindow->removeEventFilter(this);
    m_hostWindow = window;
    if (m_hostWindow)
        m_hostWindow->installEventFilter(this);
}

bool ScintillaView::eventFilter(QObject* watched, QEvent* ev)
{
    if (watched == m_hostWindow && ev->type() == QEvent::PlatformSurface) {
        const auto type = static_cast<QPlatformSurfaceEvent*>(ev)->surfaceEventType();
        if (type == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            park();
        else if (type == QPlatformSurfaceEvent::SurfaceCreated)
            QTimer::singleShot(0, this, [this] { unpark(); });
    }
    return QWidget::eventFilter(watched, ev);
}

bool ScintillaView::event(QEvent* ev)
{
    switch (ev->type()) {
    case QEvent::ParentAboutToChange:
        park();
        break;
    case QEvent::ParentChange:
    case QEvent::WinIdChange:
    case QEvent::Show:
        unpark();
        break;
    default:
        break;
    }
    return QWidget::event(ev);
}

bool ScintillaView::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(eventType);
    MSG* msg = static_cast<MSG*>(message);
    switch (msg->message) {
    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(msg->lParam);
        if (hdr && hdr->hwndFrom == static_cast<HWND>(m_hwnd)) {
            emit notify(reinterpret_cast<SCNotification*>(msg->lParam));
            *result = 0;
            return true;
        }
        break;
    }
    case WM_COMMAND:
        if (reinterpret_cast<HWND>(msg->lParam) == static_cast<HWND>(m_hwnd)) {
            switch (HIWORD(msg->wParam)) {
            case SCEN_SETFOCUS:
                emit focusGained();
                break;
            case SCEN_KILLFOCUS:
                emit focusLost();
                break;
            default:
                break;
            }
            *result = 0;
            return true;
        }
        break;
    case WM_SIZE:
        if (msg->hwnd == reinterpret_cast<HWND>(winId()) && m_hwnd && !m_parked) {
            SetWindowPos(static_cast<HWND>(m_hwnd),
                nullptr,
                0,
                0,
                LOWORD(msg->lParam),
                HIWORD(msg->lParam),
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        break;
    default:
        break;
    }
    return false;
}

#else
// ===================================================================== Qt port

namespace {

class HiddenScintilla final : public SciSender {
public:
    HiddenScintilla()
    {
        m_edit = new ScintillaEditBase(nullptr);
        m_edit->setAttribute(Qt::WA_DontShowOnScreen);
        QObject::connect(m_edit, &ScintillaEditBase::notify, notifierInstance(), [](Scintilla::NotificationData* scn) {
            emit notifierInstance() -> notify(reinterpret_cast<SCNotification*>(scn));
        });
        send(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT | SC_PERFORMED_UNDO | SC_PERFORMED_REDO);
    }

    sptr_t send(unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0) const override
    { return m_edit->send(msg, wParam, lParam); }

private:
    ScintillaEditBase* m_edit = nullptr;
};

} // namespace

SciSender& ScintillaView::hidden()
{
    static HiddenScintilla* instance = new HiddenScintilla();
    return *instance;
}

ScintillaView::ScintillaView(QWidget* parent)
    : QWidget(parent ? parent : parkingWidget())
{
    m_edit = new ScintillaEditBase(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_edit);
    setFocusProxy(m_edit);

    connect(m_edit, &ScintillaEditBase::notify, this, [this](Scintilla::NotificationData* scn) {
        emit notify(reinterpret_cast<SCNotification*>(scn));
    });
    connect(m_edit, &ScintillaEditBase::focusChanged, this, [this](bool focused) {
        if (focused)
            emit focusGained();
        else
            emit focusLost();
    });
    connect(m_edit, &ScintillaEditBase::uriDropped, this, [this](const QString& uri) {
        emit urlsDropped(QList<QUrl>{QUrl(uri)});
    });

    viewRegistry().append(this);
}

ScintillaView::~ScintillaView()
{ viewRegistry().removeAll(this); }

sptr_t ScintillaView::send(unsigned int msg, uptr_t wParam, sptr_t lParam) const
{ return m_edit->send(msg, wParam, lParam); }

void* ScintillaView::nativeHandle() const
{ return m_edit; }

void ScintillaView::focusEditor()
{ m_edit->setFocus(Qt::OtherFocusReason); }

bool ScintillaView::editorHasFocus() const
{ return m_edit->hasFocus(); }

void ScintillaView::syncGeometry() {}

void ScintillaView::resizeEvent(QResizeEvent* ev)
{ QWidget::resizeEvent(ev); }

void ScintillaView::focusInEvent(QFocusEvent* ev)
{ QWidget::focusInEvent(ev); }

bool ScintillaView::event(QEvent* ev)
{ return QWidget::event(ev); }

bool ScintillaView::eventFilter(QObject* watched, QEvent* ev)
{ return QWidget::eventFilter(watched, ev); }

#endif

} // namespace EditorNS
