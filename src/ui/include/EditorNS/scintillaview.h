#ifndef NQQ_SCINTILLAVIEW_H
#define NQQ_SCINTILLAVIEW_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QWidget>

#include <Scintilla.h>

#ifndef Q_OS_WIN
class ScintillaEditBase;
#endif

namespace EditorNS {

/** Anything that accepts Scintilla messages. */
class SciSender {
public:
    virtual ~SciSender() = default;
    virtual sptr_t send(unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0) const = 0;

    sptr_t sendStr(unsigned int msg, uptr_t wParam, const QByteArray& str) const
    { return send(msg, wParam, reinterpret_cast<sptr_t>(str.constData())); }
    sptr_t sendStr(unsigned int msg, uptr_t wParam, const char* str) const
    { return send(msg, wParam, reinterpret_cast<sptr_t>(str)); }
    /** Reads a string returned by a message that follows the (len, char*) convention. */
    QByteArray getStr(unsigned int msg, uptr_t wParam = 0) const;
};

/**
 * @brief A Scintilla editing view.
 *
 * On Windows this hosts a real Win32 "Scintilla" window (HWND) as a child of a native Qt widget.
 * That HWND is what Notepad++ plugins receive as NppData::_scintillaMainHandle /
 * _scintillaSecondHandle, so SendMessage(), SCI_GETDIRECTFUNCTION and window subclassing done by
 * plugins all operate on a genuine Scintilla control.
 *
 * On other platforms it wraps the Scintilla Qt port (ScintillaEditBase).
 *
 * As in Notepad++, a view displays one document at a time: documents (buffers) are swapped with
 * SCI_SETDOCPOINTER when the user switches tabs. The view itself stays alive for the whole life of
 * its window, so its handle is stable.
 */
class ScintillaView : public QWidget, public SciSender {
    Q_OBJECT
public:
    explicit ScintillaView(QWidget* parent = nullptr);
    ~ScintillaView() override;

    sptr_t send(unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0) const override;

    /** HWND of the Scintilla window on Windows, ScintillaEditBase* elsewhere. */
    void* nativeHandle() const;

    sptr_t document() const { return send(SCI_GETDOCPOINTER); }
    void setDocument(sptr_t doc) { send(SCI_SETDOCPOINTER, 0, doc); }

    /** Gives keyboard focus to the Scintilla control itself. */
    void focusEditor();
    bool editorHasFocus() const;

    /**
     * @brief A hidden Scintilla instance used to operate on documents that are not displayed
     *        in any view (Notepad++ uses the same technique, "_invisibleEditView").
     */
    static SciSender& hidden();
    /** Emits notifications coming from the hidden instance. */
    static QObject* hiddenNotifier();

    /** Widget used to hold views that are currently not displayed. */
    static QWidget* parkingWidget();

    /** Returns the view owning @p handle (HWND / ScintillaEditBase*), or nullptr. */
    static ScintillaView* fromNativeHandle(void* handle);
    static const QList<ScintillaView*>& allViews();

signals:
    /** Every Scintilla notification (SCN_*) of this view. */
    void notify(SCNotification* scn);
    void focusGained();
    void focusLost();
    void urlsDropped(QList<QUrl> urls);

protected:
    void resizeEvent(QResizeEvent* ev) override;
    void focusInEvent(QFocusEvent* ev) override;
    bool event(QEvent* ev) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    QPaintEngine* paintEngine() const override { return nullptr; }
#endif

private:
    void syncGeometry();

#ifdef Q_OS_WIN
    void* m_hwnd = nullptr;
    sptr_t (*m_directFn)(sptr_t, unsigned int, uptr_t, sptr_t) = nullptr;
    sptr_t m_directPtr = 0;
    bool m_parked = false;
#else
    ScintillaEditBase* m_edit = nullptr;
#endif
};

/** Signals emitted for notifications of the hidden Scintilla instance. */
class HiddenScintillaNotifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void notify(SCNotification* scn);
};

} // namespace EditorNS

#endif // NQQ_SCINTILLAVIEW_H
