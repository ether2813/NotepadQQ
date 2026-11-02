#include "include/EditorNS/editor.h"

#include "include/EditorNS/scintillaview.h"
#include "include/nqqsettings.h"

#include <QBuffer>
#include <QDir>
#include <QFontDatabase>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QShowEvent>
#include <QTextDocument>
#include <QTimer>

#include <algorithm>
#include <cstring>

namespace EditorNS {

namespace {

constexpr int kSmartHighlightIndicator = 29; // SCE_UNIVERSAL_FOUND_STYLE_SMART in Notepad++
constexpr int kBaseModEventMask =
    SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT | SC_PERFORMED_UNDO | SC_PERFORMED_REDO | SC_MOD_CHANGEINDICATOR;
int g_extraModEventFlags = 0;

/** Document currently loaded in the hidden Scintilla instance. */
sptr_t g_hiddenDocument = 0;

QList<Editor*>& editorRegistry()
{
    static QList<Editor*> editors;
    return editors;
}

/**
 * Converts between UTF-16 offsets (QString indexes) and UTF-8 byte positions (Scintilla
 * positions) by walking the document buffer. Optimised for monotonic queries.
 */
class Utf16Mapper {
public:
    Utf16Mapper(const char* buffer, sptr_t length)
        : m_buf(reinterpret_cast<const unsigned char*>(buffer))
        , m_len(length)
    {
    }

    sptr_t toByte(qsizetype utf16)
    {
        if (utf16 < m_u16)
            reset();
        while (m_u16 < utf16 && m_byte < m_len)
            advance();
        return m_byte;
    }

    qsizetype toUtf16(sptr_t byte)
    {
        if (byte < m_byte)
            reset();
        while (m_byte < byte && m_byte < m_len)
            advance();
        return m_u16;
    }

private:
    void reset()
    {
        m_byte = 0;
        m_u16 = 0;
    }

    void advance()
    {
        const unsigned char c = m_buf[m_byte];
        int len = 1;
        if (c >= 0xF0 && c < 0xF8)
            len = 4;
        else if (c >= 0xE0)
            len = c < 0xF0 ? 3 : 1;
        else if (c >= 0xC2)
            len = 2;
        if (len > 1) {
            if (m_byte + len > m_len) {
                len = 1;
            } else {
                for (int i = 1; i < len; ++i) {
                    if ((m_buf[m_byte + i] & 0xC0) != 0x80) {
                        len = 1;
                        break;
                    }
                }
            }
        }
        m_byte += len;
        m_u16 += (len == 4) ? 2 : 1;
    }

    const unsigned char* m_buf;
    sptr_t m_len;
    sptr_t m_byte = 0;
    qsizetype m_u16 = 0;
};

QRegularExpression makeRegex(const QString& pattern, const QString& modifiers)
{
    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (modifiers.contains(QLatin1Char('i')))
        options |= QRegularExpression::CaseInsensitiveOption;
    if (modifiers.contains(QLatin1Char('m')))
        options |= QRegularExpression::MultilineOption;
    // Treat \r, \n and \r\n as line terminators for ^, $ and '.' as JavaScript does.
    return QRegularExpression(QStringLiteral("(*ANYCRLF)") + pattern, options);
}

bool hasGroupReuseTokens(const QString& replacement)
{
    static const QRegularExpression re(QStringLiteral("\\\\([1-9])"));
    return re.match(replacement).hasMatch();
}

/** Substitutes \1..\9 with the captured groups; unmatched tokens are removed (Notepad++ behaviour). */
QString applyReusedGroups(QString replacement, const QStringList& groups)
{
    for (int i = 1; i < groups.size() && i <= 9; ++i)
        replacement.replace(QStringLiteral("\\") + QString::number(i), groups.at(i));
    static const QRegularExpression re(QStringLiteral("\\\\([1-9])"));
    replacement.replace(re, QString());
    return replacement;
}

/** Must match enum class SearchMode in src/ui/include/Search/searchhelpers.h */
constexpr int kSearchModeRegex = 3;

QVariantMap lineChMap(const QPair<int, int>& lc)
{ return QVariantMap{{"line", lc.first}, {"ch", lc.second}}; }

} // namespace

// ============================================================ construction

EditorEvents* EditorEvents::instance()
{
    static EditorEvents* events = new EditorEvents(qApp);
    return events;
}

Editor::Editor(QWidget* parent)
    : QWidget(parent)
{
    QString themeName = NqqSettings::getInstance().Appearance.getColorScheme();
    fullConstructor(themeFromName(themeName));
}

Editor::Editor(const Theme& theme, QWidget* parent)
    : QWidget(parent)
{ fullConstructor(theme); }

void Editor::fullConstructor(const Theme& theme)
{
    static const bool hiddenConnected = [] {
        auto* notifier = static_cast<HiddenScintillaNotifier*>(ScintillaView::hiddenNotifier());
        QObject::connect(notifier, &HiddenScintillaNotifier::notify, notifier, [](SCNotification* scn) {
            if (Editor* editor = Editor::fromDocument(g_hiddenDocument))
                editor->handleNotification(scn, false);
        });
        return true;
    }();
    Q_UNUSED(hiddenConnected);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    setLayout(m_layout);

    SciSender& hidden = ScintillaView::hidden();
    m_document = hidden.send(SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_DEFAULT);
    hidden.send(SCI_SETDOCPOINTER, 0, m_document);
    g_hiddenDocument = m_document;
    hidden.send(SCI_SETCODEPAGE, SC_CP_UTF8);
    hidden.send(SCI_SETEOLMODE, SC_EOL_LF);
    hidden.send(SCI_ALLOCATELINECHARACTERINDEX, SC_LINECHARACTERINDEX_UTF16);
    hidden.send(SCI_SETUNDOCOLLECTION, 1);

    editorRegistry().append(this);

    m_contentChangedTimer = new QTimer(this);
    m_contentChangedTimer->setSingleShot(true);
    m_contentChangedTimer->setInterval(30);
    connect(m_contentChangedTimer, &QTimer::timeout, this, [this] {
        updateLineNumberMargin();
        emit contentChanged();
        const bool clean = isClean();
        m_lastReportedClean = clean;
        emit cleanChanged(clean);
    });

    m_cursorActivityTimer = new QTimer(this);
    m_cursorActivityTimer->setSingleShot(true);
    m_cursorActivityTimer->setInterval(30);
    connect(m_cursorActivityTimer, &QTimer::timeout, this, [this] { emit cursorActivity(documentInfo()); });

    m_themeName = theme.name;
    m_smartIndent = NqqSettings::getInstance().General.getSmartIndentation();
    setLanguage(nullptr);

    emit EditorEvents::instance() -> editorCreated(this);
}

Editor::~Editor()
{
    emit EditorEvents::instance() -> editorAboutToBeDestroyed(this);
    editorRegistry().removeAll(this);

    if (m_view)
        detachView(true);
    // Never let a shared view be destroyed together with this widget.
    for (ScintillaView* view : findChildren<ScintillaView*>(Qt::FindDirectChildrenOnly)) {
        if (view != m_privateView) {
            view->hide();
            view->setParent(ScintillaView::parkingWidget());
        }
    }
    for (ScintillaView* view : ScintillaView::allViews()) {
        if (view->document() == m_document)
            view->setDocument(0);
    }
    SciSender& hidden = ScintillaView::hidden();
    if (g_hiddenDocument == m_document) {
        hidden.send(SCI_SETDOCPOINTER, 0, 0);
        g_hiddenDocument = hidden.send(SCI_GETDOCPOINTER);
    }
    hidden.send(SCI_RELEASEDOCUMENT, 0, m_document);
    m_document = 0;
}

Editor* Editor::takeNewEditor()
{ return new Editor(); }

void Editor::addEditorToBuffer(const int /*howMany*/) {}

void Editor::invalidateEditorBuffer() {}

Editor* Editor::fromDocument(sptr_t document)
{
    if (!document)
        return nullptr;
    for (Editor* editor : editorRegistry()) {
        if (editor->m_document == document)
            return editor;
    }
    return nullptr;
}

const QList<Editor*>& Editor::allEditors()
{ return editorRegistry(); }

Editor* Editor::fromBufferId(quintptr id)
{
    for (Editor* editor : editorRegistry()) {
        if (editor->bufferId() == id)
            return editor;
    }
    return nullptr;
}

void Editor::addModEventFlags(int flags)
{
    g_extraModEventFlags |= flags;
    for (ScintillaView* view : ScintillaView::allViews())
        view->send(SCI_SETMODEVENTMASK, modEventMask());
}

int Editor::modEventMask()
{ return kBaseModEventMask | g_extraModEventFlags; }

const SciSender& Editor::sender() const
{
    if (m_view && m_view->document() == m_document)
        return *m_view;
    SciSender& hidden = ScintillaView::hidden();
    if (g_hiddenDocument != m_document || hidden.send(SCI_GETDOCPOINTER) != m_document) {
        hidden.send(SCI_SETDOCPOINTER, 0, m_document);
        g_hiddenDocument = m_document;
    }
    return hidden;
}

sptr_t Editor::sci(unsigned int msg, uptr_t wParam, sptr_t lParam) const
{ return sender().send(msg, wParam, lParam); }

// ============================================================ views

void Editor::showEvent(QShowEvent* ev)
{
    QWidget::showEvent(ev);
    if (!m_view && !m_managedByTabWidget) {
        // Standalone editor (e.g. the preview in the preferences dialog).
        if (!m_privateView)
            m_privateView = new ScintillaView(this);
        attachView(m_privateView);
    }
}

void Editor::attachView(ScintillaView* view)
{
    if (!view)
        return;
    if (m_view == view && view->document() == m_document && view->parentWidget() == this)
        return;
    if (m_view && m_view != view)
        detachView(true);

    // Take the view away from the editor that currently displays a document in it.
    for (Editor* other : editorRegistry()) {
        if (other != this && other->m_view == view)
            other->detachView(false);
    }

    m_view = view;
    if (view->parentWidget() != this || m_layout->indexOf(view) < 0)
        m_layout->addWidget(view, 1);
    view->show();
    view->setDocument(m_document);

    connect(view, &ScintillaView::notify, this, &Editor::on_viewNotify, Qt::UniqueConnection);
    connect(view, &ScintillaView::focusGained, this, &Editor::gotFocus, Qt::UniqueConnection);
    connect(view, &ScintillaView::urlsDropped, this, &Editor::urlsDropped, Qt::UniqueConnection);

    applyViewSettings();
    restoreViewState();
}

void Editor::detachView(bool park)
{
    if (!m_view)
        return;
    ScintillaView* view = m_view;
    if (view->document() == m_document)
        saveViewState();
    disconnect(view, nullptr, this, nullptr);
    m_layout->removeWidget(view);
    m_view = nullptr;
    if (park && view != m_privateView) {
        view->hide();
        view->setParent(ScintillaView::parkingWidget());
    }
}

void Editor::on_viewNotify(SCNotification* scn)
{ handleNotification(scn, true); }

void Editor::applyViewSettings()
{
    if (!m_view)
        return;
    const SciSender& s = *m_view;
#ifdef Q_OS_WIN
    const qreal scale = m_view->devicePixelRatioF();
#else
    const qreal scale = 1.0;
#endif

    s.send(SCI_SETMODEVENTMASK, modEventMask());
    s.send(SCI_SETMULTIPLESELECTION, 1);
    s.send(SCI_SETADDITIONALSELECTIONTYPING, 1);
    s.send(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
    s.send(SCI_SETVIRTUALSPACEOPTIONS, SCVS_RECTANGULARSELECTION);
    s.send(SCI_SETSCROLLWIDTHTRACKING, 1);
    s.send(SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
    s.send(SCI_SETCARETLINEVISIBLEALWAYS, 1);
#ifdef Q_OS_WIN
    s.send(SCI_SETFONTQUALITY, SC_EFF_QUALITY_LCD_OPTIMIZED);
#endif

    s.send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    s.send(SCI_SETMARGINWIDTHN, 1, 0);
    s.send(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
    s.send(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
    s.send(SCI_SETMARGINSENSITIVEN, 2, 1);
    s.send(SCI_SETMARGINWIDTHN, 2, qRound(14 * scale));
    s.send(SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);
    s.send(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);

    applyStyles();

    s.send(SCI_SETWRAPMODE, m_lineWrap ? SC_WRAP_WORD : SC_WRAP_NONE);
    s.send(SCI_SETVIEWEOL, m_eolVisible);
    s.send(SCI_SETVIEWWS, (m_whitespaceVisible || m_tabsVisible) ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    s.send(SCI_SETOVERTYPE, m_overwrite);
    s.send(SCI_SETBIDIRECTIONAL, m_rtl ? SC_BIDIRECTIONAL_R2L : SC_BIDIRECTIONAL_DISABLED);

    const int base = m_font.size > 0 ? m_font.size : 10;
    s.send(SCI_SETZOOM, qBound(-10, qRound((m_zoomFactor - 1.0) * base), 60));
    updateLineNumberMargin();
}

void Editor::applyStyles()
{
    if (!m_view)
        return;
    StyleManager::instance().applyStyles(*m_view, m_currentLanguage, m_themeName, m_font);
}

void Editor::updateLineNumberMargin()
{
    if (!m_view || m_view->document() != m_document)
        return;
    if (!m_lineNumbersVisible) {
        m_view->send(SCI_SETMARGINWIDTHN, 0, 0);
        return;
    }
    const sptr_t lines = m_view->send(SCI_GETLINECOUNT);
    const int digits = std::max(3, static_cast<int>(QString::number(lines).size()));
    const QByteArray sample = QByteArray("_") + QByteArray(digits, '9');
    const sptr_t width = m_view->sendStr(SCI_TEXTWIDTH, STYLE_LINENUMBER, sample);
    if (m_view->send(SCI_GETMARGINWIDTHN, 0) != width + 4)
        m_view->send(SCI_SETMARGINWIDTHN, 0, width + 4);
}

void Editor::saveViewState()
{
    if (!m_view)
        return;
    const SciSender& s = *m_view;
    ViewState st;
    st.valid = true;
    st.firstVisibleLine = s.send(SCI_DOCLINEFROMVISIBLE, s.send(SCI_GETFIRSTVISIBLELINE));
    st.xOffset = s.send(SCI_GETXOFFSET);
    st.textHeight = std::max<int>(1, static_cast<int>(s.send(SCI_TEXTHEIGHT, 0)));
    st.selectionMode = static_cast<int>(s.send(SCI_GETSELECTIONMODE));
    st.mainSelection = static_cast<int>(s.send(SCI_GETMAINSELECTION));
    const sptr_t count = s.send(SCI_GETSELECTIONS);
    for (sptr_t i = 0; i < count; ++i)
        st.selections.append({s.send(SCI_GETSELECTIONNANCHOR, i), s.send(SCI_GETSELECTIONNCARET, i)});
    for (sptr_t line = s.send(SCI_CONTRACTEDFOLDNEXT, 0); line >= 0; line = s.send(SCI_CONTRACTEDFOLDNEXT, line + 1))
        st.foldedLines.append(line);
    m_viewState = st;
}

void Editor::restoreViewState()
{
    if (!m_view || !m_viewState.valid)
        return;
    const SciSender& s = *m_view;
    const ViewState st = m_viewState;
    const sptr_t length = s.send(SCI_GETLENGTH);

    if (!st.foldedLines.isEmpty()) {
        const sptr_t last = st.foldedLines.last();
        s.send(SCI_COLOURISE, 0, s.send(SCI_POSITIONFROMLINE, last + 1));
        for (sptr_t line : st.foldedLines) {
            if (s.send(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG)
                s.send(SCI_FOLDLINE, line, SC_FOLDACTION_CONTRACT);
        }
    }

    s.send(SCI_SETSELECTIONMODE, st.selectionMode);
    bool first = true;
    for (const auto& sel : st.selections) {
        const sptr_t anchor = std::clamp<sptr_t>(sel.first, 0, length);
        const sptr_t caret = std::clamp<sptr_t>(sel.second, 0, length);
        if (first)
            s.send(SCI_SETSELECTION, caret, anchor);
        else
            s.send(SCI_ADDSELECTION, caret, anchor);
        first = false;
    }
    if (st.mainSelection >= 0 && st.mainSelection < st.selections.size())
        s.send(SCI_SETMAINSELECTION, st.mainSelection);

    s.send(SCI_SETFIRSTVISIBLELINE, s.send(SCI_VISIBLEFROMDOCLINE, st.firstVisibleLine));
    s.send(SCI_SETXOFFSET, st.xOffset);
    if (st.scrollToCaret) {
        s.send(SCI_SCROLLCARET);
        m_viewState.scrollToCaret = false;
    }
}

// ============================================================ notifications

void Editor::handleNotification(SCNotification* scn, bool fromView)
{
    switch (scn->nmhdr.code) {
    case SCN_MODIFIED:
        if (scn->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
            // Every change (including undo/redo) gets a new generation, so that the backup
            // service never mistakes modified contents for already saved ones.
            ++m_changeCounter;
            scheduleContentChanged();
        }
        break;
    case SCN_SAVEPOINTREACHED:
    case SCN_SAVEPOINTLEFT: {
        const bool clean = isClean();
        if (clean != m_lastReportedClean) {
            m_lastReportedClean = clean;
            emit cleanChanged(clean);
        }
        break;
    }
    case SCN_UPDATEUI:
        if (fromView && (scn->updated & (SC_UPDATE_SELECTION | SC_UPDATE_CONTENT))) {
            scheduleCursorActivity();
            updateBraceHighlight();
            if (scn->updated & SC_UPDATE_SELECTION)
                updateSmartHighlight();
        }
        break;
    case SCN_CHARADDED:
        if (fromView && m_smartIndent) {
            const sptr_t eol = sci(SCI_GETEOLMODE);
            const int newline = eol == SC_EOL_CR ? '\r' : '\n';
            if (scn->ch == newline)
                autoIndentNewLine();
        }
        break;
    case SCN_ZOOM:
        if (fromView && m_view) {
            const int base = m_font.size > 0 ? m_font.size : 10;
            m_zoomFactor = 1.0 + static_cast<qreal>(m_view->send(SCI_GETZOOM)) / base;
        }
        break;
    case SCN_FOCUSIN:
        if (fromView)
            emit gotFocus();
        break;
    default:
        break;
    }

    if (fromView)
        emit scintillaNotification(scn);
}

void Editor::scheduleContentChanged()
{
    if (!m_contentChangedTimer->isActive())
        m_contentChangedTimer->start();
}

void Editor::scheduleCursorActivity()
{
    if (!m_cursorActivityTimer->isActive())
        m_cursorActivityTimer->start();
}

void Editor::updateBraceHighlight()
{
    if (!m_view)
        return;
    const SciSender& s = *m_view;
    if (s.send(SCI_GETSELECTIONS) > 1) {
        s.send(SCI_BRACEHIGHLIGHT, INVALID_POSITION, INVALID_POSITION);
        return;
    }
    auto isBrace = [](int ch) { return ch && strchr("()[]{}", ch) != nullptr; };
    sptr_t pos = s.send(SCI_GETCURRENTPOS);
    sptr_t bracePos = INVALID_POSITION;
    if (pos > 0 && isBrace(static_cast<int>(s.send(SCI_GETCHARAT, pos - 1))))
        bracePos = pos - 1;
    else if (isBrace(static_cast<int>(s.send(SCI_GETCHARAT, pos))))
        bracePos = pos;

    if (bracePos == INVALID_POSITION) {
        s.send(SCI_BRACEHIGHLIGHT, INVALID_POSITION, INVALID_POSITION);
        return;
    }
    const sptr_t match = s.send(SCI_BRACEMATCH, bracePos, 0);
    if (match == INVALID_POSITION)
        s.send(SCI_BRACEBADLIGHT, bracePos);
    else
        s.send(SCI_BRACEHIGHLIGHT, bracePos, match);
}

void Editor::updateSmartHighlight()
{
    if (!m_view)
        return;
    const SciSender& s = *m_view;
    const sptr_t length = s.send(SCI_GETLENGTH);
    s.send(SCI_SETINDICATORCURRENT, kSmartHighlightIndicator);
    s.send(SCI_INDICATORCLEARRANGE, 0, length);

    if (s.send(SCI_GETSELECTIONS) != 1)
        return;
    const sptr_t start = s.send(SCI_GETSELECTIONSTART);
    const sptr_t end = s.send(SCI_GETSELECTIONEND);
    if (start == end || end - start > 256)
        return;
    // Only whole words are highlighted, like CodeMirror's "highlightSelectionMatches".
    if (s.send(SCI_WORDSTARTPOSITION, start, 1) != start || s.send(SCI_WORDENDPOSITION, start, 1) != end)
        return;
    const QByteArray word = textRange(start, end).toUtf8();
    if (word.trimmed().isEmpty())
        return;

    const sptr_t firstLine = s.send(SCI_DOCLINEFROMVISIBLE, s.send(SCI_GETFIRSTVISIBLELINE));
    const sptr_t lastLine = firstLine + s.send(SCI_LINESONSCREEN) + 1;
    const sptr_t rangeStart = s.send(SCI_POSITIONFROMLINE, firstLine);
    sptr_t rangeEnd = s.send(SCI_GETLINEENDPOSITION, lastLine);
    if (rangeEnd < rangeStart)
        rangeEnd = length;

    const sptr_t oldTargetStart = s.send(SCI_GETTARGETSTART);
    const sptr_t oldTargetEnd = s.send(SCI_GETTARGETEND);
    const sptr_t oldFlags = s.send(SCI_GETSEARCHFLAGS);
    s.send(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | SCFIND_WHOLEWORD);
    sptr_t from = rangeStart;
    while (from < rangeEnd) {
        s.send(SCI_SETTARGETRANGE, from, rangeEnd);
        const sptr_t found = s.sendStr(SCI_SEARCHINTARGET, word.size(), word);
        if (found < 0)
            break;
        const sptr_t foundEnd = s.send(SCI_GETTARGETEND);
        if (found != start)
            s.send(SCI_INDICATORFILLRANGE, found, foundEnd - found);
        from = std::max(foundEnd, found + 1);
    }
    s.send(SCI_SETSEARCHFLAGS, oldFlags);
    s.send(SCI_SETTARGETRANGE, oldTargetStart, oldTargetEnd);
}

void Editor::autoIndentNewLine()
{
    if (!m_view || m_view->send(SCI_GETSELECTIONS) != 1)
        return;
    const SciSender& s = *m_view;
    const sptr_t line = s.send(SCI_LINEFROMPOSITION, s.send(SCI_GETCURRENTPOS));
    if (line <= 0)
        return;
    const sptr_t indent = s.send(SCI_GETLINEINDENTATION, line - 1);
    if (indent <= 0)
        return;
    s.send(SCI_SETLINEINDENTATION, line, indent);
    s.send(SCI_GOTOPOS, s.send(SCI_GETLINEINDENTPOSITION, line));
}

// ============================================================ position helpers

QString Editor::textRange(sptr_t start, sptr_t end) const
{
    if (end <= start)
        return QString();
    QByteArray buf(static_cast<qsizetype>(end - start) + 1, '\0');
    Sci_TextRangeFull tr;
    tr.chrg.cpMin = start;
    tr.chrg.cpMax = end;
    tr.lpstrText = buf.data();
    const sptr_t len = sci(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&tr));
    buf.resize(static_cast<qsizetype>(len));
    return QString::fromUtf8(buf);
}

QString Editor::rawText() const
{
    const SciSender& s = sender();
    const sptr_t len = s.send(SCI_GETLENGTH);
    const char* ptr = reinterpret_cast<const char*>(s.send(SCI_GETCHARACTERPOINTER));
    if (!ptr || len <= 0)
        return QString();
    return QString::fromUtf8(ptr, static_cast<qsizetype>(len));
}

sptr_t Editor::positionFromLineCol(sptr_t line, sptr_t column) const
{
    const SciSender& s = sender();
    if (line < 0)
        return 0;
    if (line >= s.send(SCI_GETLINECOUNT))
        return s.send(SCI_GETLENGTH);
    const sptr_t start = s.send(SCI_POSITIONFROMLINE, line);
    const sptr_t end = s.send(SCI_GETLINEENDPOSITION, line);
    if (column <= 0)
        return start;
    if (column >= s.send(SCI_COUNTCODEUNITS, start, end))
        return end;
    return s.send(SCI_POSITIONRELATIVECODEUNITS, start, column);
}

QPair<int, int> Editor::lineColFromPosition(sptr_t pos) const
{
    const SciSender& s = sender();
    const sptr_t line = s.send(SCI_LINEFROMPOSITION, pos);
    const sptr_t start = s.send(SCI_POSITIONFROMLINE, line);
    return {static_cast<int>(line), static_cast<int>(s.send(SCI_COUNTCODEUNITS, start, pos))};
}

QVector<QPair<sptr_t, sptr_t>> Editor::currentSelections() const
{
    QVector<QPair<sptr_t, sptr_t>> sels;
    if (m_view && m_view->document() == m_document) {
        const sptr_t count = m_view->send(SCI_GETSELECTIONS);
        for (sptr_t i = 0; i < count; ++i)
            sels.append({m_view->send(SCI_GETSELECTIONNANCHOR, i), m_view->send(SCI_GETSELECTIONNCARET, i)});
    } else if (m_viewState.valid && !m_viewState.selections.isEmpty()) {
        const sptr_t length = sci(SCI_GETLENGTH);
        for (const auto& sel : m_viewState.selections)
            sels.append({std::clamp<sptr_t>(sel.first, 0, length), std::clamp<sptr_t>(sel.second, 0, length)});
    } else {
        sels.append({0, 0});
    }
    std::sort(sels.begin(), sels.end(), [](const auto& a, const auto& b) {
        return std::min(a.first, a.second) < std::min(b.first, b.second);
    });
    return sels;
}

void Editor::setSelections(const QVector<QPair<sptr_t, sptr_t>>& sels, int main)
{
    if (sels.isEmpty())
        return;
    if (m_view && m_view->document() == m_document) {
        const SciSender& s = *m_view;
        s.send(SCI_SETSELECTION, sels.first().second, sels.first().first);
        for (int i = 1; i < sels.size(); ++i)
            s.send(SCI_ADDSELECTION, sels.at(i).second, sels.at(i).first);
        if (main >= 0 && main < sels.size())
            s.send(SCI_SETMAINSELECTION, main);
    } else {
        if (!m_viewState.valid) {
            m_viewState = ViewState();
            m_viewState.valid = true;
        }
        m_viewState.selections = sels;
        m_viewState.mainSelection = main;
        m_viewState.selectionMode = SC_SEL_STREAM;
    }
}

void Editor::ensureVisible(sptr_t pos)
{
    if (m_view && m_view->document() == m_document) {
        m_view->send(SCI_ENSUREVISIBLEENFORCEPOLICY, m_view->send(SCI_LINEFROMPOSITION, pos));
        m_view->send(SCI_SCROLLRANGE, pos, pos);
        m_view->send(SCI_SCROLLCARET);
    } else {
        m_viewState.scrollToCaret = true;
    }
}

QString Editor::normalizeEol(QString text) const
{
    if (text.contains(QLatin1Char('\r'))) {
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    }
    return text;
}

QByteArray Editor::toDocumentEol(const QString& text) const
{
    QString t = normalizeEol(text);
    if (m_endOfLineSequence != QLatin1String("\n"))
        t.replace(QLatin1Char('\n'), m_endOfLineSequence);
    return t.toUtf8();
}

void Editor::replaceRange(sptr_t start, sptr_t end, const QByteArray& utf8)
{
    const SciSender& s = sender();
    s.send(SCI_SETTARGETRANGE, start, end);
    s.send(SCI_REPLACETARGET, utf8.size(), reinterpret_cast<sptr_t>(utf8.constData()));
}

void Editor::editLines(const std::function<QString(const QString&)>& transform)
{
    const SciSender& s = sender();
    s.send(SCI_BEGINUNDOACTION);
    for (sptr_t line = s.send(SCI_GETLINECOUNT) - 1; line >= 0; --line) {
        const sptr_t start = s.send(SCI_POSITIONFROMLINE, line);
        const sptr_t end = s.send(SCI_GETLINEENDPOSITION, line);
        const QString original = textRange(start, end);
        const QString changed = transform(original);
        if (changed != original)
            replaceRange(start, end, changed.toUtf8());
    }
    s.send(SCI_ENDUNDOACTION);
}

QVariantMap Editor::documentInfo()
{
    const SciSender& s = sender();
    QVariantMap map;
    const auto sels = currentSelections();
    sptr_t caret = sels.first().second;
    if (m_view && m_view->document() == m_document)
        caret = m_view->send(SCI_GETCURRENTPOS);
    const auto cursor = lineColFromPosition(caret);
    map["cursor"] = QVariantList{cursor.first, cursor.second};

    int selLines = 0;
    sptr_t selLength = 0;
    for (const auto& sel : sels) {
        const sptr_t start = std::min(sel.first, sel.second);
        const sptr_t end = std::max(sel.first, sel.second);
        selLines += static_cast<int>(s.send(SCI_LINEFROMPOSITION, end) - s.send(SCI_LINEFROMPOSITION, start) + 1);
        selLength += s.send(SCI_COUNTCODEUNITS, start, end);
    }
    selLength += sels.size() - 1;
    if (selLength == 0)
        selLines = 1;
    map["selections"] = QVariantList{selLines, static_cast<qlonglong>(selLength)};

    const sptr_t lines = s.send(SCI_GETLINECOUNT);
    const sptr_t lastLine = lines - 1;
    const sptr_t lastLineStart = s.send(SCI_POSITIONFROMLINE, lastLine);
    const sptr_t totalUnits = s.send(SCI_INDEXPOSITIONFROMLINE, lastLine, SC_LINECHARACTERINDEX_UTF16) +
                              s.send(SCI_COUNTCODEUNITS, lastLineStart, s.send(SCI_GETLENGTH));
    map["content"] = QVariantList{static_cast<qlonglong>(lines), static_cast<qlonglong>(totalUnits)};
    return map;
}

// ============================================================ public API

void Editor::setFocus()
{
    if (m_view)
        m_view->focusEditor();
    else
        QWidget::setFocus();
}

void Editor::clearFocus()
{
    if (m_view)
        m_view->clearFocus();
    QWidget::clearFocus();
}

/**
 * Automatically converts local relative file names to absolute ones.
 */
void Editor::setFilePath(const QUrl& filename)
{
    QUrl old = m_filePath;
    QUrl newUrl = filename;

    if (newUrl.isLocalFile())
        newUrl = QUrl::fromLocalFile(QFileInfo(filename.toLocalFile()).absoluteFilePath());

    m_filePath = newUrl;
    emit fileNameChanged(old, newUrl);
}

/**
 * Always returns an absolute url.
 */
QUrl Editor::filePath() const
{ return m_filePath; }

QString Editor::tabName() const
{ return m_tabName; }

void Editor::setTabName(const QString& name)
{ m_tabName = name; }

QtPromise::QPromise<bool> Editor::isCleanP()
{ return QtPromise::QPromise<bool>::resolve(isClean()); }

bool Editor::isClean()
{ return !m_forceDirty && sci(SCI_GETMODIFY) == 0; }

QtPromise::QPromise<void> Editor::markClean()
{
    m_forceDirty = false;
    sci(SCI_SETSAVEPOINT);
    m_lastReportedClean = isClean();
    emit cleanChanged(m_lastReportedClean);
    return QtPromise::QPromise<void>::resolve();
}

QtPromise::QPromise<void> Editor::markDirty()
{
    m_forceDirty = true;
    m_lastReportedClean = false;
    emit cleanChanged(false);
    return QtPromise::QPromise<void>::resolve();
}

QtPromise::QPromise<int> Editor::getHistoryGeneration()
{ return QtPromise::QPromise<int>::resolve(m_changeCounter); }

void Editor::setLanguage(const Language* lang)
{
    if (lang == nullptr)
        lang = LanguageService::getInstance().lookupById("plaintext");
    if (lang == nullptr || m_currentLanguage == lang)
        return;
    if (!m_customIndentationMode)
        setIndentationMode(lang);
    m_currentLanguage = lang;
    StyleManager::instance().applyLexer(sender(), lang);
    applyStyles();
    emit currentLanguageChanged(m_currentLanguage->id, m_currentLanguage->name);
}

void Editor::setLanguage(const QString& language)
{
    auto& cache = LanguageService::getInstance();
    auto lang = cache.lookupById(language);
    if (lang != nullptr)
        setLanguage(lang);
}

void Editor::setLanguageFromFilePath(const QString& filePath)
{
    auto name = QFileInfo(filePath).fileName();

    auto& cache = LanguageService::getInstance();
    auto lang = cache.lookupByFileName(name);
    if (lang != nullptr) {
        setLanguage(lang);
        return;
    }
    lang = cache.lookupByExtension(name);
    if (lang != nullptr)
        setLanguage(lang);
}

void Editor::setLanguageFromFilePath()
{ setLanguageFromFilePath(filePath().toString()); }

QtPromise::QPromise<void> Editor::setIndentationMode(const Language* lang)
{
    const auto& s = NqqSettings::getInstance().Languages;
    const bool useDefaults = s.getUseDefaultSettings(lang->id);
    const auto& langId = useDefaults ? "default" : lang->id;

    return setIndentationMode(!s.getIndentWithSpaces(langId), s.getTabSize(langId));
}

QtPromise::QPromise<void> Editor::setIndentationMode(const bool useTabs, const int size)
{
    const SciSender& s = sender();
    s.send(SCI_SETUSETABS, useTabs);
    if (size > 0) {
        s.send(SCI_SETTABWIDTH, size);
        s.send(SCI_SETINDENT, size);
    }
    return QtPromise::QPromise<void>::resolve();
}

IndentationMode Editor::indentationMode()
{
    IndentationMode out;
    out.useTabs = sci(SCI_GETUSETABS) != 0;
    const sptr_t indent = sci(SCI_GETINDENT);
    out.size = static_cast<int>(indent > 0 ? indent : sci(SCI_GETTABWIDTH));
    return out;
}

QtPromise::QPromise<IndentationMode> Editor::indentationModeP()
{ return QtPromise::QPromise<IndentationMode>::resolve(indentationMode()); }

void Editor::setCustomIndentationMode(const bool useTabs, const int size)
{
    m_customIndentationMode = true;
    setIndentationMode(useTabs, size);
}

void Editor::setCustomIndentationMode(const bool useTabs)
{
    m_customIndentationMode = true;
    setIndentationMode(useTabs, 0);
}

void Editor::clearCustomIndentationMode()
{
    m_customIndentationMode = false;
    setIndentationMode(getLanguage());
}

bool Editor::isUsingCustomIndentationMode() const
{ return m_customIndentationMode; }

void Editor::setSmartIndent(bool enabled)
{ m_smartIndent = enabled; }

QtPromise::QPromise<void> Editor::setValue(const QString& value)
{
    auto lang = LanguageService::getInstance().lookupByContent(value);
    if (lang != nullptr)
        setLanguage(lang);

    const QByteArray utf8 = toDocumentEol(value);
    const SciSender& s = sender();
    s.send(SCI_BEGINUNDOACTION);
    s.send(SCI_CLEARALL);
    s.send(SCI_APPENDTEXT, utf8.size(), reinterpret_cast<sptr_t>(utf8.constData()));
    s.send(SCI_ENDUNDOACTION);
    setSelections({{0, 0}});
    if (m_view && m_view->document() == m_document)
        m_view->send(SCI_SCROLLCARET);
    return QtPromise::QPromise<void>::resolve();
}

QString Editor::value()
{ return normalizeEol(rawText()); }

bool Editor::fileOnDiskChanged() const
{ return m_fileOnDiskChanged; }

void Editor::setFileOnDiskChanged(bool fileOnDiskChanged)
{ m_fileOnDiskChanged = fileOnDiskChanged; }

void Editor::sendMessage(const QString msg, const QVariant data)
{ dispatch(msg, data); }

void Editor::sendMessage(const QString msg)
{ dispatch(msg, 0); }

QtPromise::QPromise<QVariant> Editor::asyncSendMessageWithResultP(const QString msg, const QVariant data)
{ return QtPromise::QPromise<QVariant>::resolve(dispatch(msg, data)); }

QtPromise::QPromise<QVariant> Editor::asyncSendMessageWithResultP(const QString msg)
{ return asyncSendMessageWithResultP(msg, 0); }

std::shared_future<QVariant> Editor::asyncSendMessageWithResult(
    const QString msg, const QVariant data, std::function<void(QVariant)> callback)
{
    const QVariant result = dispatch(msg, data);
    std::promise<QVariant> promise;
    promise.set_value(result);
    if (callback)
        callback(result);
    return promise.get_future().share();
}

std::shared_future<QVariant> Editor::asyncSendMessageWithResult(
    const QString msg, std::function<void(QVariant)> callback)
{ return asyncSendMessageWithResult(msg, 0, callback); }

void Editor::setZoomFactor(const qreal& factor)
{
    m_zoomFactor = qBound<qreal>(0.10, factor, 14.0);
    if (m_view && m_view->document() == m_document) {
        const int base = m_font.size > 0 ? m_font.size : 10;
        m_view->send(SCI_SETZOOM, qBound(-10, qRound((m_zoomFactor - 1.0) * base), 60));
        updateLineNumberMargin();
    }
}

qreal Editor::zoomFactor() const
{ return m_zoomFactor; }

void Editor::setSelectionsText(const QStringList& texts, SelectMode mode)
{
    QVariantMap data{{"text", texts}};
    switch (mode) {
    case SelectMode::After:
        data.insert("select", "after");
        break;
    case SelectMode::Before:
        data.insert("select", "before");
        break;
    default:
        data.insert("select", "selected");
        break;
    }
    dispatch("C_CMD_SET_SELECTIONS_TEXT", data);
}

void Editor::setSelectionsText(const QStringList& texts)
{ setSelectionsText(texts, SelectMode::After); }

void Editor::insertBanner(QWidget* banner)
{ m_layout->insertWidget(0, banner); }

void Editor::removeBanner(QWidget* banner)
{
    if (banner != m_view && m_layout->indexOf(banner) >= 0) {
        m_layout->removeWidget(banner);
        emit bannerRemoved(banner);
    }
}

void Editor::removeBanner(QString objectName)
{
    for (auto&& banner : findChildren<QWidget*>(objectName))
        removeBanner(banner);
}

void Editor::setLineWrap(const bool wrap)
{
    m_lineWrap = wrap;
    if (m_view)
        m_view->send(SCI_SETWRAPMODE, wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
}

void Editor::setEOLVisible(const bool showeol)
{
    m_eolVisible = showeol;
    if (m_view)
        m_view->send(SCI_SETVIEWEOL, showeol);
}

void Editor::setWhitespaceVisible(const bool showspace)
{
    m_whitespaceVisible = showspace;
    if (m_view)
        m_view->send(SCI_SETVIEWWS, (m_whitespaceVisible || m_tabsVisible) ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
}

void Editor::setTabsVisible(bool visible)
{
    m_tabsVisible = visible;
    if (m_view)
        m_view->send(SCI_SETVIEWWS, (m_whitespaceVisible || m_tabsVisible) ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
}

void Editor::setMathEnabled(const bool /*enabled*/)
{
    // MathJax rendering was a feature of the browser based editor and is not available natively.
}

QtPromise::QPromise<QPair<int, int>> Editor::cursorPositionP()
{ return QtPromise::QPromise<QPair<int, int>>::resolve(cursorPosition()); }

void Editor::requestDocumentInfo()
{ emit documentInfoRequested(documentInfo()); }

QPair<int, int> Editor::cursorPosition()
{
    if (m_view && m_view->document() == m_document)
        return lineColFromPosition(m_view->send(SCI_GETCURRENTPOS));
    const auto sels = currentSelections();
    const int main = std::clamp(m_viewState.mainSelection, 0, static_cast<int>(sels.size()) - 1);
    return lineColFromPosition(sels.at(main).second);
}

void Editor::setCursorPosition(const int line, const int column)
{
    const sptr_t pos = positionFromLineCol(line, column);
    setSelections({{pos, pos}});
    ensureVisible(pos);
}

void Editor::setCursorPosition(const QPair<int, int>& position)
{ setCursorPosition(position.first, position.second); }

void Editor::setCursorPosition(const Cursor& cursor)
{ setCursorPosition(cursor.line, cursor.column); }

void Editor::setSelection(int fromLine, int fromCol, int toLine, int toCol)
{
    const sptr_t anchor = positionFromLineCol(fromLine, fromCol);
    const sptr_t caret = positionFromLineCol(toLine, toCol);
    setSelections({{anchor, caret}});
    ensureVisible(caret);
}

QPair<int, int> Editor::scrollPosition()
{
    if (m_view && m_view->document() == m_document) {
        const sptr_t textHeight = m_view->send(SCI_TEXTHEIGHT, 0);
        return {static_cast<int>(m_view->send(SCI_GETXOFFSET)),
            static_cast<int>(m_view->send(SCI_GETFIRSTVISIBLELINE) * textHeight)};
    }
    if (!m_viewState.valid)
        return {0, 0};
    return {
        static_cast<int>(m_viewState.xOffset), static_cast<int>(m_viewState.firstVisibleLine * m_viewState.textHeight)};
}

void Editor::setScrollPosition(const int left, const int top)
{
    if (m_view && m_view->document() == m_document) {
        const sptr_t textHeight = std::max<sptr_t>(1, m_view->send(SCI_TEXTHEIGHT, 0));
        m_view->send(SCI_SETFIRSTVISIBLELINE, top / textHeight);
        m_view->send(SCI_SETXOFFSET, left);
        return;
    }
    if (!m_viewState.valid) {
        m_viewState = ViewState();
        m_viewState.valid = true;
        m_viewState.selections = {{0, 0}};
    }
    m_viewState.xOffset = left;
    m_viewState.firstVisibleLine = top / std::max(1, m_viewState.textHeight);
    m_viewState.scrollToCaret = false;
}

void Editor::setScrollPosition(const QPair<int, int>& position)
{ setScrollPosition(position.first, position.second); }

QString Editor::endOfLineSequence() const
{ return m_endOfLineSequence; }

void Editor::setEndOfLineSequence(const QString& newLineSequence)
{
    m_endOfLineSequence = newLineSequence;
    int mode = SC_EOL_LF;
    if (newLineSequence == QLatin1String("\r\n"))
        mode = SC_EOL_CRLF;
    else if (newLineSequence == QLatin1String("\r"))
        mode = SC_EOL_CR;
    const SciSender& s = sender();
    s.send(SCI_SETEOLMODE, mode);
    if (s.send(SCI_GETLENGTH) > 0)
        s.send(SCI_CONVERTEOLS, mode);
}

int Editor::eolFormat() const
{
    if (m_endOfLineSequence == QLatin1String("\r\n"))
        return 0;
    if (m_endOfLineSequence == QLatin1String("\r"))
        return 1;
    return 2;
}

void Editor::setFont(QString fontFamily, int fontSize, double lineHeight)
{
    m_font.family = fontFamily.isNull() ? QString() : fontFamily;
    m_font.size = fontSize;
    m_font.lineHeight = lineHeight;
    if (m_view) {
        applyStyles();
        updateLineNumberMargin();
    }
}

void Editor::setLineNumbersVisible(bool visible)
{
    m_lineNumbersVisible = visible;
    updateLineNumberMargin();
}

QTextCodec* Editor::codec() const
{ return m_codec; }

void Editor::setCodec(QTextCodec* codec)
{ m_codec = codec; }

bool Editor::bom() const
{ return m_bom; }

void Editor::setBom(bool bom)
{ m_bom = bom; }

Editor::Theme Editor::themeFromName(QString name)
{
    if (name.isEmpty() || name == "default")
        return Theme();
    if (StyleManager::instance().themeNames().contains(name))
        return Theme(name, "");
    return Theme();
}

QList<Editor::Theme> Editor::themes()
{
    QList<Theme> out;
    for (const QString& name : StyleManager::instance().themeNames())
        out.append(Theme(name, ""));
    return out;
}

void Editor::setTheme(Theme theme)
{
    m_themeName = theme.name;
    StyleManager::instance().setCurrentTheme(theme.name);
    // Themes may add keywords to lexer classes, so the lexer is refreshed too.
    StyleManager::instance().applyLexer(sender(), m_currentLanguage);
    if (m_view) {
        applyStyles();
        updateLineNumberMargin();
    }
}

QList<Selection> Editor::selections()
{
    QList<Selection> out;
    for (const auto& sel : currentSelections()) {
        const auto from = lineColFromPosition(sel.first);
        const auto to = lineColFromPosition(sel.second);
        Selection s;
        s.from.line = from.first;
        s.from.column = from.second;
        s.to.line = to.first;
        s.to.column = to.second;
        out.append(s);
    }
    return out;
}

QtPromise::QPromise<QStringList> Editor::selectedTexts()
{ return QtPromise::QPromise<QStringList>::resolve(dispatch("C_FUN_GET_SELECTIONS_TEXT", 0).toStringList()); }

void Editor::setOverwrite(bool overwrite)
{
    m_overwrite = overwrite;
    if (m_view)
        m_view->send(SCI_SETOVERTYPE, overwrite);
}

QtPromise::QPromise<std::pair<IndentationMode, bool>> Editor::detectDocumentIndentation()
{
    const QVariantMap indent = dispatch("C_FUN_DETECT_INDENTATION_MODE", 0).toMap();
    IndentationMode out;
    out.useTabs = true;
    out.size = 4;
    const bool found = indent.value("found", false).toBool();
    if (found) {
        out.useTabs = indent.value("useTabs", true).toBool();
        out.size = indent.value("size", 4).toInt();
    }
    return QtPromise::QPromise<std::pair<IndentationMode, bool>>::resolve(std::make_pair(out, found));
}

void Editor::print(std::shared_ptr<QPrinter> printer)
{
    QTextDocument doc;
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!m_font.family.isEmpty())
        font.setFamily(m_font.family);
    font.setPointSize(m_font.size > 0 ? m_font.size : 10);
    doc.setDefaultFont(font);
    doc.setPlainText(value());
    doc.print(printer.get());
}

QtPromise::QPromise<QByteArray> Editor::printToPdf(const QPageLayout& pageLayout)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    {
        QPdfWriter writer(&buffer);
        writer.setPageLayout(pageLayout);
        writer.setTitle(m_tabName);
        QTextDocument doc;
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (!m_font.family.isEmpty())
            font.setFamily(m_font.family);
        font.setPointSize(m_font.size > 0 ? m_font.size : 10);
        doc.setDefaultFont(font);
        doc.setPlainText(value());
        doc.print(&writer);
    }
    buffer.close();
    if (data.isEmpty())
        return QtPromise::QPromise<QByteArray>::reject(QByteArray());
    return QtPromise::QPromise<QByteArray>::resolve(data);
}

QtPromise::QPromise<QString> Editor::getCurrentWord()
{ return QtPromise::QPromise<QString>::resolve(dispatch("C_FUN_GET_CURRENT_WORD", 0).toString()); }

QtPromise::QPromise<int> Editor::lineCount()
{ return QtPromise::QPromise<int>::resolve(static_cast<int>(sci(SCI_GETLINECOUNT))); }

// ============================================================ search & replace

QVariant Editor::search(const QString& regex, const QString& modifiers, bool forward)
{
    const QRegularExpression re = makeRegex(regex, modifiers);
    if (!re.isValid())
        return false;

    const SciSender& s = sender();
    const sptr_t len = s.send(SCI_GETLENGTH);
    const char* buf = reinterpret_cast<const char*>(s.send(SCI_GETCHARACTERPOINTER));
    const QString text = QString::fromUtf8(buf, static_cast<qsizetype>(len));
    Utf16Mapper map(buf, len);

    const auto sels = currentSelections();
    const auto& mainSel = sels.first();
    const sptr_t selStart = std::min(mainSel.first, mainSel.second);
    const sptr_t selEnd = std::max(mainSel.first, mainSel.second);
    const qsizetype from = map.toUtf16(forward ? selEnd : selStart);

    QRegularExpressionMatch match;
    if (forward) {
        match = re.match(text, from);
        if (match.hasMatch() && match.capturedLength() == 0 && match.capturedStart() == from && selStart == selEnd)
            match = re.match(text, from + 1);
        if (!match.hasMatch())
            match = re.match(text, 0);
    } else {
        QRegularExpressionMatch last;
        QRegularExpressionMatch lastBefore;
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            if (m.capturedStart() < from && m.capturedEnd() <= from)
                lastBefore = m;
            last = m;
        }
        match = lastBefore.hasMatch() ? lastBefore : last;
    }

    if (!match.hasMatch())
        return false;

    const sptr_t start = map.toByte(match.capturedStart());
    const sptr_t end = map.toByte(match.capturedEnd());
    if (forward)
        setSelections({{start, end}});
    else
        setSelections({{end, start}});
    ensureVisible(forward ? end : start);
    return match.capturedTexts();
}

QVariant Editor::replace(const QVariantList& args)
{
    const QString regexStr = args.value(0).toString();
    const QString modifiers = args.value(1).toString();
    const bool forward = args.value(2).toBool();
    QString replacement = args.value(3).toString();
    const int searchMode = args.value(4).toInt();

    const auto sels = currentSelections();
    const sptr_t start = std::min(sels.first().first, sels.first().second);
    const sptr_t end = std::max(sels.first().first, sels.first().second);
    if (start != end) {
        if (searchMode == kSearchModeRegex && hasGroupReuseTokens(replacement)) {
            const QRegularExpressionMatch m = makeRegex(regexStr, modifiers).match(textRange(start, end));
            if (m.hasMatch())
                replacement = applyReusedGroups(replacement, m.capturedTexts());
        }
        const QByteArray utf8 = toDocumentEol(replacement);
        replaceRange(start, end, utf8);
        const sptr_t caret = start + utf8.size();
        setSelections({{caret, caret}});
    }
    return search(regexStr, modifiers, forward);
}

int Editor::replaceAll(const QVariantList& args)
{
    const QRegularExpression re = makeRegex(args.value(0).toString(), args.value(1).toString());
    const QString replacement = args.value(2).toString();
    const int searchMode = args.value(3).toInt();
    if (!re.isValid())
        return 0;
    const bool reuseGroups = searchMode == kSearchModeRegex && hasGroupReuseTokens(replacement);

    const SciSender& s = sender();
    const sptr_t len = s.send(SCI_GETLENGTH);
    const char* buf = reinterpret_cast<const char*>(s.send(SCI_GETCHARACTERPOINTER));
    const QString text = QString::fromUtf8(buf, static_cast<qsizetype>(len));
    Utf16Mapper map(buf, len);

    struct Edit {
        sptr_t start;
        sptr_t end;
        QByteArray text;
    };
    std::vector<Edit> edits;
    const QByteArray plainReplacement = toDocumentEol(replacement);
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const sptr_t start = map.toByte(m.capturedStart());
        const sptr_t end = map.toByte(m.capturedEnd());
        edits.push_back({start,
            end,
            reuseGroups ? toDocumentEol(applyReusedGroups(replacement, m.capturedTexts())) : plainReplacement});
    }
    if (edits.empty())
        return 0;

    s.send(SCI_BEGINUNDOACTION);
    for (auto e = edits.rbegin(); e != edits.rend(); ++e)
        replaceRange(e->start, e->end, e->text);
    s.send(SCI_ENDUNDOACTION);
    return static_cast<int>(edits.size());
}

int Editor::searchSelectAll(const QVariantList& args)
{
    const QRegularExpression re = makeRegex(args.value(0).toString(), args.value(1).toString());
    if (!re.isValid())
        return 0;
    const SciSender& s = sender();
    const sptr_t len = s.send(SCI_GETLENGTH);
    const char* buf = reinterpret_cast<const char*>(s.send(SCI_GETCHARACTERPOINTER));
    const QString text = QString::fromUtf8(buf, static_cast<qsizetype>(len));
    Utf16Mapper map(buf, len);

    QVector<QPair<sptr_t, sptr_t>> sels;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedLength() == 0)
            continue;
        const sptr_t start = map.toByte(m.capturedStart());
        const sptr_t end = map.toByte(m.capturedEnd());
        sels.append({start, end});
    }
    if (!sels.isEmpty())
        setSelections(sels);
    return static_cast<int>(sels.size());
}

// ============================================================ legacy protocol

QVariant Editor::dispatch(const QString& msg, const QVariant& data)
{
    const SciSender& s = sender();

    if (msg == "C_CMD_SET_VALUE") {
        setValue(data.toString());
    } else if (msg == "C_FUN_GET_VALUE") {
        return value();
    } else if (msg == "C_CMD_MARK_CLEAN") {
        markClean();
    } else if (msg == "C_CMD_MARK_DIRTY") {
        markDirty();
    } else if (msg == "C_FUN_IS_CLEAN") {
        return isClean();
    } else if (msg == "C_FUN_GET_HISTORY_GENERATION") {
        return m_changeCounter;
    } else if (msg == "C_CMD_SET_LANGUAGE") {
        const QString key = data.toString();
        for (const Language& l : LanguageService::getInstance().languages()) {
            if (l.id == key || l.mime == key || l.mode == key) {
                setLanguage(&l);
                break;
            }
        }
    } else if (msg == "C_CMD_SET_INDENTATION_MODE") {
        const QVariantMap m = data.toMap();
        setIndentationMode(m.value("useTabs").toBool(), m.value("size").toInt());
    } else if (msg == "C_FUN_GET_INDENTATION_MODE") {
        const IndentationMode mode = indentationMode();
        return QVariantMap{{"useTabs", mode.useTabs}, {"size", mode.size}};
    } else if (msg == "C_FUN_GET_SELECTIONS_TEXT") {
        QStringList out;
        for (const auto& sel : currentSelections())
            out.append(normalizeEol(textRange(std::min(sel.first, sel.second), std::max(sel.first, sel.second))));
        return out;
    } else if (msg == "C_CMD_SET_SELECTIONS_TEXT") {
        const QVariantMap m = data.toMap();
        const QStringList texts = m.value("text").toStringList();
        const QString select = m.value("select").toString();
        const auto sels = currentSelections();
        const bool perSelection = texts.size() == sels.size();
        const QString joined = texts.join("\n");

        QVector<QPair<sptr_t, sptr_t>> newSels;
        sptr_t delta = 0;
        s.send(SCI_BEGINUNDOACTION);
        for (int i = 0; i < sels.size(); ++i) {
            const sptr_t start = std::min(sels[i].first, sels[i].second) + delta;
            const sptr_t end = std::max(sels[i].first, sels[i].second) + delta;
            const QByteArray utf8 = toDocumentEol(perSelection ? texts.at(i) : joined);
            replaceRange(start, end, utf8);
            const sptr_t newEnd = start + utf8.size();
            delta += utf8.size() - (end - start);
            if (select == "before")
                newSels.append({start, start});
            else if (select == "selected")
                newSels.append({start, newEnd});
            else
                newSels.append({newEnd, newEnd});
        }
        s.send(SCI_ENDUNDOACTION);
        setSelections(newSels);
    } else if (msg == "C_FUN_GET_SELECTIONS") {
        QVariantList out;
        for (const auto& sel : currentSelections()) {
            out.append(QVariantMap{{"anchor", lineChMap(lineColFromPosition(sel.first))},
                {"head", lineChMap(lineColFromPosition(sel.second))}});
        }
        return out;
    } else if (msg == "C_CMD_SET_SELECTION") {
        const QVariantList l = data.toList();
        setSelection(l.value(0).toInt(), l.value(1).toInt(), l.value(2).toInt(), l.value(3).toInt());
    } else if (msg == "C_FUN_GET_TEXT_LENGTH") {
        return static_cast<int>(value().size());
    } else if (msg == "C_FUN_GET_LINE_COUNT") {
        return static_cast<int>(s.send(SCI_GETLINECOUNT));
    } else if (msg == "C_FUN_GET_CURSOR") {
        const auto c = cursorPosition();
        return QVariantList{c.first, c.second};
    } else if (msg == "C_CMD_SET_CURSOR") {
        const QVariantList l = data.toList();
        setCursorPosition(l.value(0).toInt(), l.value(1).toInt());
    } else if (msg == "C_CMD_SET_RTL" || msg == "C_CMD_SET_LTR") {
        m_rtl = msg == "C_CMD_SET_RTL";
        if (m_view)
            m_view->send(SCI_SETBIDIRECTIONAL, m_rtl ? SC_BIDIRECTIONAL_R2L : SC_BIDIRECTIONAL_DISABLED);
    } else if (msg == "C_FUN_GET_SCROLL_POS") {
        const auto p = scrollPosition();
        return QVariantList{p.first, p.second};
    } else if (msg == "C_CMD_SET_SCROLL_POS") {
        const QVariantList l = data.toList();
        setScrollPosition(l.value(0).toInt(), l.value(1).toInt());
    } else if (msg == "C_CMD_SELECT_ALL") {
        setSelections({{0, s.send(SCI_GETLENGTH)}});
    } else if (msg == "C_CMD_UNDO") {
        s.send(SCI_UNDO);
    } else if (msg == "C_CMD_REDO") {
        s.send(SCI_REDO);
    } else if (msg == "C_CMD_CLEAR_HISTORY") {
        s.send(SCI_EMPTYUNDOBUFFER);
    } else if (msg == "C_CMD_SET_LINE_WRAP") {
        setLineWrap(data.toBool());
    } else if (msg == "C_CMD_SHOW_END_OF_LINE") {
        setEOLVisible(data.toBool());
    } else if (msg == "C_CMD_SHOW_WHITESPACE") {
        setWhitespaceVisible(data.toBool());
    } else if (msg == "C_CMD_SET_TABS_VISIBLE") {
        setTabsVisible(data.toBool());
    } else if (msg == "C_FUN_SEARCH") {
        const QVariantList l = data.toList();
        return search(l.value(0).toString(), l.value(1).toString(), l.value(2).toBool());
    } else if (msg == "C_FUN_REPLACE") {
        return replace(data.toList());
    } else if (msg == "C_FUN_REPLACE_ALL") {
        return replaceAll(data.toList());
    } else if (msg == "C_FUN_SEARCH_SELECT_ALL") {
        return searchSelectAll(data.toList());
    } else if (msg == "C_FUN_GET_LANGUAGES") {
        QVariantList out;
        for (const Language& l : LanguageService::getInstance().languages())
            out.append(QVariantMap{{"id", l.id}, {"name", l.name}});
        return out;
    } else if (msg == "C_CMD_SET_THEME") {
        setTheme(Theme(data.toMap().value("name").toString(), data.toMap().value("path").toString()));
    } else if (msg == "C_CMD_SET_FONT") {
        const QVariantMap m = data.toMap();
        setFont(m.value("family").toString(), m.value("size").toInt(), m.value("lineHeight").toDouble());
    } else if (msg == "C_CMD_SET_LINE_NUMBERS_VISIBLE") {
        setLineNumbersVisible(data.toBool());
    } else if (msg == "C_CMD_SET_OVERWRITE") {
        setOverwrite(data.toBool());
    } else if (msg == "C_CMD_SET_SMART_INDENT") {
        setSmartIndent(data.toBool());
        return data.toBool();
    } else if (msg == "C_CMD_SET_FOCUS") {
        setFocus();
    } else if (msg == "C_CMD_BLUR") {
        clearFocus();
    } else if (msg == "C_FUN_DETECT_INDENTATION_MODE") {
        static const QRegularExpression indented(QStringLiteral("^([ ]{2,}|[\\t]+)[^ \\t]+?"));
        const sptr_t lines = std::min<sptr_t>(s.send(SCI_GETLINECOUNT), 100);
        for (sptr_t i = 0; i < lines; ++i) {
            const QString line = textRange(s.send(SCI_POSITIONFROMLINE, i), s.send(SCI_GETLINEENDPOSITION, i));
            const QRegularExpressionMatch m = indented.match(line);
            if (!m.hasMatch())
                continue;
            if (line.startsWith(QLatin1Char('\t')))
                return QVariantMap{{"found", true}, {"useTabs", true}, {"size", 0}};
            const int size = static_cast<int>(m.capturedLength(1));
            if (size == 2 || size == 4 || size == 8)
                return QVariantMap{{"found", true}, {"useTabs", false}, {"size", size}};
            return QVariantMap{{"found", false}};
        }
        return QVariantMap{{"found", false}};
    } else if (msg == "C_CMD_DISPLAY_PRINT_STYLE" || msg == "C_CMD_DISPLAY_NORMAL_STYLE") {
        // Printing is rendered natively, no style switch is needed.
    } else if (msg == "C_FUN_GET_CURRENT_WORD") {
        const auto cur = cursorPosition();
        const QString line =
            textRange(s.send(SCI_POSITIONFROMLINE, cur.first), s.send(SCI_GETLINEENDPOSITION, cur.first));
        static const QRegularExpression left(QStringLiteral("[^\\w]?(\\w*)$"));
        static const QRegularExpression right(QStringLiteral("^(\\w*)[^\\w]?"));
        QString word;
        const auto ml = left.match(line.left(cur.second));
        const auto mr = right.match(line.mid(cur.second));
        if (ml.hasMatch())
            word += ml.captured(1);
        if (mr.hasMatch())
            word += mr.captured(1);
        return word;
    } else if (msg == "C_CMD_DUPLICATE_LINE") {
        s.send(SCI_LINEDUPLICATE);
    } else if (msg == "C_CMD_MOVE_LINE_UP") {
        s.send(SCI_MOVESELECTEDLINESUP);
    } else if (msg == "C_CMD_MOVE_LINE_DOWN") {
        s.send(SCI_MOVESELECTEDLINESDOWN);
    } else if (msg == "C_CMD_TRANSPOSE_LINE") {
        if (lineColFromPosition(s.send(SCI_GETCURRENTPOS)).first == 0)
            return false;
        s.send(SCI_LINETRANSPOSE);
    } else if (msg == "C_CMD_DELETE_LINE") {
        s.send(SCI_LINEDELETE);
    } else if (msg == "C_CMD_TRIM_LEADING_TRAILING_SPACE") {
        editLines([](const QString& x) { return x.trimmed(); });
    } else if (msg == "C_CMD_TRIM_TRAILING_SPACE") {
        static const QRegularExpression trailing(QStringLiteral("\\s+$"));
        editLines([](const QString& x) { return QString(x).remove(trailing); });
    } else if (msg == "C_CMD_TRIM_LEADING_SPACE") {
        static const QRegularExpression leading(QStringLiteral("^\\s+"));
        editLines([](const QString& x) { return QString(x).remove(leading); });
    } else if (msg == "C_CMD_ENABLE_MATH") {
        setMathEnabled(data.toBool());
    } else if (msg == "C_FUN_IS_MATH_ENABLED") {
        return false;
    } else if (msg == "C_CMD_TAB_TO_SPACE") {
        const int tab = std::max<int>(1, static_cast<int>(s.send(SCI_GETTABWIDTH)));
        editLines([tab](const QString& x) {
            QString out;
            int col = 0;
            for (QChar c : x) {
                if (c == QLatin1Char('\t')) {
                    const int n = tab - (col % tab);
                    out += QString(n, QLatin1Char(' '));
                    col += n;
                } else {
                    out += c;
                    ++col;
                }
            }
            return out;
        });
    } else if (msg == "C_CMD_SPACE_TO_TAB_ALL" || msg == "C_CMD_SPACE_TO_TAB_LEADING") {
        const int tab = std::max<int>(1, static_cast<int>(s.send(SCI_GETTABWIDTH)));
        const bool leadingOnly = msg == "C_CMD_SPACE_TO_TAB_LEADING";
        editLines([tab, leadingOnly](const QString& x) {
            static const QRegularExpression runs(QStringLiteral(" +"));
            QString out;
            qsizetype last = 0;
            int counter = 0;
            auto it = runs.globalMatch(x);
            while (it.hasNext()) {
                const auto m = it.next();
                if (leadingOnly && m.capturedStart() != 0)
                    break;
                const int start = static_cast<int>(m.capturedStart()) + counter;
                int len = static_cast<int>(m.capturedLength());
                QString result;
                const int leading = tab - (start % tab);
                if (len >= leading) {
                    result += QLatin1Char('\t');
                    len -= leading;
                }
                while (len >= tab) {
                    result += QLatin1Char('\t');
                    len -= tab;
                }
                result += QString(len, QLatin1Char(' '));
                counter -= static_cast<int>(m.capturedLength() - result.size());
                out += x.mid(last, m.capturedStart() - last) + result;
                last = m.capturedEnd();
                if (leadingOnly)
                    break;
            }
            out += x.mid(last);
            return out;
        });
    } else if (msg == "C_CMD_EOL_TO_SPACE") {
        QString text = value();
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        const QByteArray utf8 = text.toUtf8();
        s.send(SCI_BEGINUNDOACTION);
        replaceRange(0, s.send(SCI_GETLENGTH), utf8);
        s.send(SCI_ENDUNDOACTION);
    } else if (msg == "C_CMD_GET_DOCUMENT_INFO") {
        requestDocumentInfo();
    } else {
        qWarning() << "Unknown editor message" << msg;
    }
    return QVariant();
}

} // namespace EditorNS
