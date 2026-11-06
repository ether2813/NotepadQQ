#ifndef EDITOR_H
#define EDITOR_H

#include "include/EditorNS/editor_properties.h"
#include "include/EditorNS/languageservice.h"
#include "include/EditorNS/scintillaview.h"
#include "include/EditorNS/stylemanager.h"

#include <QCoreApplication>
#include <QPageLayout>
#include <QPageSize>
#include <QPointer>
#include <QPrinter>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWheelEvent>
#include <QWidget>
#include <QtCore5Compat/QTextCodec>
#include <QtPromise>

#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <utility>

#include <Scintilla.h>

#ifdef NQQ_CPP_CORRECTNESS_TESTS
class EditorCorrectnessAccess;
#endif

class EditorTabWidget;

namespace EditorNS {

class Editor;

/**
 * @brief Application-wide notifications about editor (buffer) lifetime, used by the
 *        Notepad++ plugin host to emit NPPN_* notifications.
 */
class EditorEvents : public QObject {
    Q_OBJECT
public:
    static EditorEvents* instance();

signals:
    void editorCreated(EditorNS::Editor* editor);
    /** Emitted at the beginning of the editor's destructor: the document is still valid. */
    void editorAboutToBeDestroyed(EditorNS::Editor* editor);

private:
    using QObject::QObject;
};

/**
 * @brief A document (buffer) edited with the native Scintilla component.
 *
 * Every Editor owns one Scintilla document. Like in Notepad++, documents are displayed by a
 * small number of long-lived Scintilla views (one per EditorTabWidget): when a tab becomes
 * current, its Editor's document is swapped into the tab widget's view with SCI_SETDOCPOINTER
 * and the per-buffer view state (selection, scroll position, folding) is restored. Operations on
 * documents that are not displayed are executed through a hidden Scintilla instance.
 *
 * The public API, including the legacy "C_CMD_* / C_FUN_*" message protocol that used to be
 * served by the CodeMirror JavaScript editor, is preserved. Messages are now executed
 * synchronously in C++, and the promise-returning methods return already-settled promises.
 *
 * Ownership: an Editor in a tab or layout is owned and destroyed by Qt's parent-child
 * hierarchy. Callers receive non-owning Editor pointers and must use QPointer when a
 * reference crosses an asynchronous boundary.
 */
class Editor : public QWidget {
    Q_OBJECT
public:
    struct Theme {
        QString name;
        QString path;
        Theme(const QString& name = "default", const QString& path = "")
        {
            this->name = name;
            this->path = path;
        }
    };

    explicit Editor(const Theme& theme, QWidget* parent = nullptr);
    explicit Editor(QWidget* parent = nullptr);
    ~Editor() override;

    /**
     * @brief Creates a new editor ready to be adopted by a Qt widget parent.
     *
     * The caller transfers ownership to Qt by adding the widget to a layout or tab widget.
     */
    static Editor* takeNewEditor();

    /** Kept for API compatibility: native editors are cheap and are not preloaded anymore. */
    static void invalidateEditorBuffer();
    static void addEditorToBuffer(const int howMany = 1);

    /**
     * @brief Just a flag that is used for marking editors that are still loading,
     * meaning for example that the Editor has been created but we still need
     * to load the file contents or setup the syntax highlighting.
     */
    bool isLoading = false;

    /**
     * @brief Give focus to the editor, so that the user can start
     *        typing. Note that calling won't automatically switch to
     *        the tab where the editor is. Use EditorTabWidget::setCurrentIndex()
     *        and TopEditorContainer::setFocus() for that.
     */
    Q_INVOKABLE void setFocus();

    /** @brief Remove the focus from the editor. */
    Q_INVOKABLE void clearFocus();

    /**
     * @brief Set the file name associated with this editor
     * @param filename full path of the file
     */
    Q_INVOKABLE void setFilePath(const QUrl& filename);

    /** @brief Get the file name associated with this editor */
    Q_INVOKABLE QUrl filePath() const;

    Q_INVOKABLE bool fileOnDiskChanged() const;
    Q_INVOKABLE void setFileOnDiskChanged(bool fileOnDiskChanged);

    enum class SelectMode { Before, After, Selected };

    void insertBanner(QWidget* banner);
    void removeBanner(QWidget* banner);
    void removeBanner(QString objectName);

    // Lower-level message wrappers:
    QtPromise::QPromise<bool> isCleanP();
    Q_INVOKABLE bool isClean();
    Q_INVOKABLE QtPromise::QPromise<void> markClean();
    Q_INVOKABLE QtPromise::QPromise<void> markDirty();

    /**
     * @brief Returns an integer that changes every time the contents change (edits, undo and
     *        redo). Used by the backup service to detect documents that need a new snapshot.
     */
    Q_INVOKABLE QtPromise::QPromise<int> getHistoryGeneration();
    /** Number of changes made to the document so far (synchronous getHistoryGeneration()). */
    int historyGeneration() const { return m_changeCounter; }

    /**
     * @brief Set the language to use for the editor.
     *        It automatically adjusts tab settings from
     *        the default configuration for the specified language.
     * @param language Language id
     */
    Q_INVOKABLE void setLanguage(const Language* language);
    Q_INVOKABLE void setLanguage(const QString& language);
    Q_INVOKABLE void setLanguageFromFilePath(const QString& filePath);
    Q_INVOKABLE void setLanguageFromFilePath();
    Q_INVOKABLE QtPromise::QPromise<void> setValue(const QString& value);
    Q_INVOKABLE QString value();

    /**
     * @brief Set custom indentation settings which may be different
     *        from the default tab settings associated with the current
     *        language.
     *        If this method is called, further calls to setLanguage()
     *        will NOT modify these tab settings. Use
     *        clearCustomIndentationMode() to reset to default settings.
     * @param useTabs
     * @param size Size of an indentation. If 0, keeps the current one.
     */
    void setCustomIndentationMode(const bool useTabs, const int size);
    void setCustomIndentationMode(const bool useTabs);
    void clearCustomIndentationMode();
    bool isUsingCustomIndentationMode() const;

    Q_INVOKABLE void setSmartIndent(bool enabled);
    Q_INVOKABLE qreal zoomFactor() const;
    Q_INVOKABLE void setZoomFactor(const qreal& factor);
    Q_INVOKABLE void setSelectionsText(const QStringList& texts, SelectMode mode);
    Q_INVOKABLE void setSelectionsText(const QStringList& texts);
    const Language* getLanguage() { return m_currentLanguage; }
    Q_INVOKABLE void setLineWrap(const bool wrap);
    Q_INVOKABLE void setEOLVisible(const bool showeol);
    Q_INVOKABLE void setWhitespaceVisible(const bool showspace);
    Q_INVOKABLE void setMathEnabled(const bool enabled);

    /**
     * @brief Get the current cursor position
     * @return a <line, column> pair.
     */
    QPair<int, int> cursorPosition();
    QtPromise::QPromise<QPair<int, int>> cursorPositionP();
    void setCursorPosition(const int line, const int column);
    void setCursorPosition(const QPair<int, int>& position);
    void setCursorPosition(const Cursor& cursor);

    /**
     * @brief Tells the editor that mainwindow needs an update on the contents,
     *        selection, and cursor position of the current document
     */
    void requestDocumentInfo();

    /**
     * @brief Get the current scroll position
     * @return a <left, top> pair.
     */
    QPair<int, int> scrollPosition();
    void setScrollPosition(const int left, const int top);
    void setScrollPosition(const QPair<int, int>& position);
    QString endOfLineSequence() const;
    void setEndOfLineSequence(const QString& endOfLineSequence);

    /**
     * @brief Applies a font family/size to the Editor.
     * @param fontFamily the family to be applied. An empty string or
     *                   nullptr denote no override.
     * @param fontSize the size to be applied. 0 denotes no override.
     */
    void setFont(QString fontFamily, int fontSize, double lineHeight);

    /**
     * @brief Toggles line numbers on/off in the editor
     */
    void setLineNumbersVisible(bool visible);

    QTextCodec* codec() const;

    /**
     * @brief Set the codec for this Editor.
     *        This method does not change the in-memory or on-screen
     *        representation of the document (which is always Unicode).
     *        It serves solely as a way to keep track of the encoding
     *        that needs to be used when the document gets saved.
     */
    void setCodec(QTextCodec* codec);

    bool bom() const;
    void setBom(bool bom);

    /** Notepad++ themes (stylers.xml format) bundled with Notepadqq or installed by the user. */
    QList<Theme> themes();
    void setTheme(Theme theme);
    static Editor::Theme themeFromName(QString name);

    QList<Selection> selections();

    /** @brief Returns the currently selected texts. */
    Q_INVOKABLE QtPromise::QPromise<QStringList> selectedTexts();

    void setOverwrite(bool overwrite);
    void setTabsVisible(bool visible);

    /**
     * @brief Detect the indentation mode used within the current document.
     * @return a pair whose first element is the document indentation, that is
     *         significative only if the second element ("found") is true.
     */
    QtPromise::QPromise<std::pair<IndentationMode, bool>> detectDocumentIndentation();
    IndentationMode indentationMode();
    QtPromise::QPromise<IndentationMode> indentationModeP();

    QtPromise::QPromise<QString> getCurrentWord();

    void setSelection(int fromLine, int fromCol, int toLine, int toCol);

    QtPromise::QPromise<int> lineCount();

    // ------------------------------------------------------------------ Native API

    /** Sends a Scintilla message to this editor's document (view if displayed, else hidden). */
    sptr_t sci(unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0) const;

    /** The Scintilla document pointer owned by this editor. */
    sptr_t document() const { return m_document; }

    /** Returns the editor owning a Scintilla document, or nullptr. */
    static Editor* fromDocument(sptr_t document);
    static const QList<Editor*>& allEditors();

    /** Stable identifier of this buffer (Notepad++ "BufferID"). */
    quintptr bufferId() const { return reinterpret_cast<quintptr>(this); }
    static Editor* fromBufferId(quintptr id);

    /** The view currently displaying this document, if any. */
    ScintillaView* view() const { return m_view; }

    /**
     * @brief Displays this document in @p view (used by EditorTabWidget). The view is moved
     *        into this widget's layout and the per-buffer view state is restored.
     */
    void attachView(ScintillaView* view);
    /**
     * @brief Saves the per-buffer view state and stops displaying this document.
     * @param park true to also move the view widget out of this editor (into the parking area).
     */
    void detachView(bool park = true);

    /**
     * @brief Extra SCN_MODIFIED flags requested by Notepad++ plugins (NPPM_ADDSCNMODIFIEDFLAGS).
     */
    static void addModEventFlags(int flags);
    static int modEventMask();

    /** Set by EditorTabWidget: managed editors never create a private view. */
    void setManagedByTabWidget(bool managed) { m_managedByTabWidget = managed; }

    /**
     * @brief Executes one message of the legacy editor protocol (see doc/editor-protocol.md).
     * @return the reply value (invalid QVariant for commands without a result)
     */
    QVariant dispatch(const QString& msg, const QVariant& data);

    /** Full text with the document's real line endings. */
    QString rawText() const;

    /** Notepad++ EOL format: 0 = Windows (CRLF), 1 = Mac (CR), 2 = Unix (LF). */
    int eolFormat() const;

private:
    friend class ::EditorTabWidget;
#ifdef NQQ_CPP_CORRECTNESS_TESTS
    friend class ::EditorCorrectnessAccess;
#endif

    struct ViewState {
        bool valid = false;
        bool scrollToCaret = false;
        int textHeight = 16;
        sptr_t firstVisibleLine = 0;
        sptr_t xOffset = 0;
        int selectionMode = 0;
        int mainSelection = 0;
        QVector<QPair<sptr_t, sptr_t>> selections; ///< (anchor, caret)
        QVector<sptr_t> foldedLines;
    };

    // These functions should only be used by EditorTabWidget to manage the tab's title. This works around
    // KDE's habit to automatically modify QTabWidget's tab titles to insert shortcut sequences (like &1).
    QString tabName() const;
    void setTabName(const QString& name);

    void fullConstructor(const Theme& theme);
    const SciSender& sender() const;
    void applyViewSettings();
    void applyStyles();
    void saveViewState();
    void restoreViewState();
    void updateLineNumberMargin();
    void handleNotification(SCNotification* scn, bool fromView);
    void scheduleContentChanged();
    void scheduleCursorActivity();
    void updateBraceHighlight();
    void updateSmartHighlight();
    void autoIndentNewLine();

    QVariantMap documentInfo();
    QString textRange(sptr_t start, sptr_t end) const;
    sptr_t positionFromLineCol(sptr_t line, sptr_t column) const;
    QPair<int, int> lineColFromPosition(sptr_t pos) const;
    QVector<QPair<sptr_t, sptr_t>> currentSelections() const; ///< (anchor, caret), sorted by start
    void setSelections(const QVector<QPair<sptr_t, sptr_t>>& sels, int main = 0);
    void ensureVisible(sptr_t pos);
    QString normalizeEol(QString text) const;
    QByteArray toDocumentEol(const QString& text) const;
    void replaceRange(sptr_t start, sptr_t end, const QByteArray& utf8);
    void editLines(const std::function<QString(const QString&)>& transform);

    QVariant search(const QString& regex, const QString& modifiers, bool forward);
    QVariant replace(const QVariantList& args);
    int replaceAll(const QVariantList& args);
    int searchSelectAll(const QVariantList& args);

    QtPromise::QPromise<void> setIndentationMode(const bool useTabs, const int size);
    QtPromise::QPromise<void> setIndentationMode(const Language*);

    sptr_t m_document = 0;
    QPointer<ScintillaView> m_view;
    ScintillaView* m_privateView = nullptr;
    bool m_managedByTabWidget = false;
    ViewState m_viewState;
    QVBoxLayout* m_layout = nullptr;
    QTimer* m_contentChangedTimer = nullptr;
    QTimer* m_cursorActivityTimer = nullptr;

    QUrl m_filePath = QUrl();
    QString m_tabName;
    bool m_fileOnDiskChanged = false;
    bool m_forceDirty = false;
    int m_changeCounter = 0;
    bool m_lastReportedClean = true;
    QString m_endOfLineSequence = "\n";
    QTextCodec* m_codec = QTextCodec::codecForName("UTF-8");
    bool m_bom = false;
    bool m_customIndentationMode = false;
    const Language* m_currentLanguage = nullptr;

    // View settings, re-applied whenever the document is shown in a view.
    QString m_themeName;
    EditorFont m_font;
    qreal m_zoomFactor = 1.0;
    bool m_lineWrap = false;
    bool m_eolVisible = false;
    bool m_whitespaceVisible = false;
    bool m_tabsVisible = false;
    bool m_lineNumbersVisible = true;
    bool m_overwrite = false;
    bool m_smartIndent = true;
    bool m_rtl = false;

protected:
    void showEvent(QShowEvent* ev) override;

private slots:
    void on_viewNotify(SCNotification* scn);

signals:
    void gotFocus();
    void mouseWheel(QWheelEvent* ev);
    void urlsDropped(QList<QUrl> urls);
    void bannerRemoved(QWidget* banner);

    // Pre-interpreted messages:
    void contentChanged();
    void cursorActivity(QMap<QString, QVariant> data);
    void documentInfoRequested(QMap<QString, QVariant> data);
    void cleanChanged(bool isClean);
    void fileNameChanged(const QUrl& oldFileName, const QUrl& newFileName);

    /** Kept for API compatibility. Native editors are ready as soon as they are constructed. */
    void editorReady();

    void currentLanguageChanged(QString id, QString name);

    /** Emitted for every Scintilla notification of the view displaying this document. */
    void scintillaNotification(SCNotification* scn);

public slots:

    // [[deprecated]]
    void sendMessage(const QString msg, const QVariant data);
    // [[deprecated]]
    void sendMessage(const QString msg);

    QtPromise::QPromise<QVariant> asyncSendMessageWithResultP(const QString msg, const QVariant data);
    QtPromise::QPromise<QVariant> asyncSendMessageWithResultP(const QString msg);

    /**
     * @brief asyncSendMessageWithResult
     * @param callback When set, the result is also delivered to the provided function.
     */
    // [[deprecated]]
    std::shared_future<QVariant> asyncSendMessageWithResult(
        const QString msg, const QVariant data, std::function<void(QVariant)> callback = nullptr);
    // [[deprecated]]
    std::shared_future<QVariant> asyncSendMessageWithResult(
        const QString msg, std::function<void(QVariant)> callback = nullptr);

    /** @brief Print the editor contents. */
    void print(std::shared_ptr<QPrinter> printer);

    /**
     * @brief Returns the content of the editor layed out in a pdf file that can be directly saved to disk.
     */
    QtPromise::QPromise<QByteArray> printToPdf(
        const QPageLayout& pageLayout = QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF()));
};

} // namespace EditorNS

#endif // EDITOR_H
