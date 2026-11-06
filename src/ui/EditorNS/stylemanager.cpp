#include "include/EditorNS/stylemanager.h"

#include "include/EditorNS/languageservice.h"
#include "include/EditorNS/scintillaview.h"
#include "include/notepadqq.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QStandardPaths>
#include <QXmlStreamReader>

#include <ILexer.h>
#include <Lexilla.h>
#include <SciLexer.h>
#include <Scintilla.h>

namespace EditorNS {

namespace {

QString attr(const QXmlStreamAttributes& a, const char* name)
{ return a.value(QLatin1String(name)).toString(); }

int intAttr(const QXmlStreamAttributes& a, const char* name, int def = -1)
{
    bool ok = false;
    const int v = a.value(QLatin1String(name)).toInt(&ok);
    return ok ? v : def;
}

NppStyle parseStyle(const QXmlStreamAttributes& a)
{
    NppStyle s;
    s.id = intAttr(a, "styleID");
    s.name = attr(a, "name");
    s.fg = StyleManager::colorFromHex(attr(a, "fgColor"));
    s.bg = StyleManager::colorFromHex(attr(a, "bgColor"));
    s.fontName = attr(a, "fontName");
    s.fontStyle = intAttr(a, "fontStyle");
    s.fontSize = intAttr(a, "fontSize");
    s.keywordClass = attr(a, "keywordClass");
    return s;
}

/**
 * Order in which Notepad++ passes its keyword classes to SCI_SETKEYWORDS, for lexers that
 * don't need special handling (see ScintillaEditView::setLexer in Notepad++).
 */
const QStringList& genericKeywordOrder()
{
    static const QStringList order = {
        "instre1", "instre2", "type1", "type2", "type3", "type4", "type5", "type6", "type7"};
    return order;
}

/** Keyword set order used by Notepad++ for the C-like languages handled by the "cpp" lexer. */
const QStringList& cppKeywordOrder()
{
    static const QStringList order = {"instre1", "type1", "type2", "instre2"};
    return order;
}

void setProperty(const SciSender& sci, const char* key, const char* value)
{ sci.send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>(key), reinterpret_cast<sptr_t>(value)); }

QString defaultMonospaceFamily()
{
#ifdef Q_OS_WIN
    return QStringLiteral("Consolas");
#else
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
#endif
}

void applyOneStyle(const SciSender& sci, int styleId, const NppStyle& s)
{
    if (styleId < 0 || styleId > 255)
        return;
    if (s.fg >= 0)
        sci.send(SCI_STYLESETFORE, styleId, s.fg);
    if (s.bg >= 0)
        sci.send(SCI_STYLESETBACK, styleId, s.bg);
    if (!s.fontName.isEmpty())
        sci.sendStr(SCI_STYLESETFONT, styleId, s.fontName.toUtf8());
    if (s.fontSize > 0)
        sci.send(SCI_STYLESETSIZE, styleId, s.fontSize);
    if (s.fontStyle >= 0) {
        sci.send(SCI_STYLESETBOLD, styleId, (s.fontStyle & 1) != 0);
        sci.send(SCI_STYLESETITALIC, styleId, (s.fontStyle & 2) != 0);
        sci.send(SCI_STYLESETUNDERLINE, styleId, (s.fontStyle & 4) != 0);
    }
}

void defineMarker(const SciSender& sci, int marker, int type, int fore, int back, int activeBack)
{
    sci.send(SCI_MARKERDEFINE, marker, type);
    sci.send(SCI_MARKERSETFORE, marker, fore);
    sci.send(SCI_MARKERSETBACK, marker, back);
    sci.send(SCI_MARKERSETBACKSELECTED, marker, activeBack);
}

} // namespace

bool NppTheme::isDark() const
{
    const int r = defaultBg & 0xFF;
    const int g = (defaultBg >> 8) & 0xFF;
    const int b = (defaultBg >> 16) & 0xFF;
    return (r * 299 + g * 587 + b * 114) / 1000 < 128;
}

StyleManager& StyleManager::instance()
{
    static StyleManager manager;
    return manager;
}

StyleManager::StyleManager() {}

int StyleManager::colorFromHex(const QString& rrggbb)
{
    if (rrggbb.size() != 6)
        return -1;
    bool ok = false;
    const int rgb = rrggbb.toInt(&ok, 16);
    if (!ok)
        return -1;
    const int r = (rgb >> 16) & 0xFF;
    const int g = (rgb >> 8) & 0xFF;
    const int b = rgb & 0xFF;
    return r | (g << 8) | (b << 16);
}

QString StyleManager::dataPath()
{ return Notepadqq::appDataPath(QStringLiteral("npp")); }

QString StyleManager::userThemesPath()
{ return Notepadqq::userConfigPath() + QStringLiteral("/themes"); }

void StyleManager::loadLanguages()
{
    if (m_languagesLoaded)
        return;
    m_languagesLoaded = true;

    QFile file(QDir(dataPath()).filePath(QStringLiteral("langs.xml")));
    if (!file.open(QIODevice::ReadOnly))
        return;

    QXmlStreamReader xml(&file);
    NppLangData* current = nullptr;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("Language")) {
                const auto a = xml.attributes();
                NppLangData data;
                data.name = attr(a, "name");
                data.extensions = attr(a, "ext").split(QLatin1Char(' '), Qt::SkipEmptyParts);
                data.commentLine = attr(a, "commentLine");
                data.commentStart = attr(a, "commentStart");
                data.commentEnd = attr(a, "commentEnd");
                current = &m_languages.insert(data.name, data).value();
            } else if (xml.name() == QLatin1String("Keywords") && current) {
                const QString cls = attr(xml.attributes(), "name");
                current->keywords.insert(cls, xml.readElementText().simplified());
            }
        } else if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("Language")) {
            current = nullptr;
        }
    }
}

const NppLangData* StyleManager::nppLanguage(const QString& nppName)
{
    loadLanguages();
    auto it = m_languages.constFind(nppName);
    return it == m_languages.constEnd() ? nullptr : &it.value();
}

QString StyleManager::themePath(const QString& name)
{
    if (name.isEmpty() || name == QLatin1String("default"))
        return QDir(dataPath()).filePath(QStringLiteral("stylers.xml"));

    const QString fileName = name + QStringLiteral(".xml");
    const QString user = QDir(userThemesPath()).filePath(fileName);
    if (QFileInfo::exists(user))
        return user;
    const QString bundled = QDir(dataPath() + QStringLiteral("/themes")).filePath(fileName);
    if (QFileInfo::exists(bundled))
        return bundled;
    return QDir(dataPath()).filePath(QStringLiteral("stylers.xml"));
}

QStringList StyleManager::themeNames()
{
    QStringList names;
    for (const QString& dir : {dataPath() + QStringLiteral("/themes"), userThemesPath()}) {
        const QFileInfoList files = QDir(dir).entryInfoList(QStringList{QStringLiteral("*.xml")}, QDir::Files);
        for (const QFileInfo& f : files) {
            if (!names.contains(f.completeBaseName()))
                names.append(f.completeBaseName());
        }
    }
    names.sort(Qt::CaseInsensitive);
    names.prepend(QStringLiteral("default"));
    return names;
}

std::unique_ptr<NppTheme> StyleManager::loadTheme(const QString& name, const QString& path) const
{
    auto theme = std::make_unique<NppTheme>();
    theme->name = name;
    theme->path = path;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return theme;

    QXmlStreamReader xml(&file);
    QString currentLexer;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("LexerType")) {
                currentLexer = attr(xml.attributes(), "name");
            } else if (xml.name() == QLatin1String("WordsStyle")) {
                NppStyle s = parseStyle(xml.attributes());
                s.keywords = xml.readElementText().simplified();
                theme->lexerStyles[currentLexer].append(s);
            } else if (xml.name() == QLatin1String("WidgetStyle")) {
                NppStyle s = parseStyle(xml.attributes());
                theme->globalStyles.insert(s.name, s);
            }
        } else if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("LexerType")) {
            currentLexer.clear();
        }
    }

    const NppStyle def = theme->global(QStringLiteral("Default Style"));
    if (def.fg >= 0)
        theme->defaultFg = def.fg;
    if (def.bg >= 0)
        theme->defaultBg = def.bg;
    return theme;
}

const NppTheme& StyleManager::theme(const QString& name)
{
    const QString key = name.isEmpty() ? QStringLiteral("default") : name;
    auto it = m_themes.find(key);
    if (it == m_themes.end()) {
        std::shared_ptr<NppTheme> loaded = loadTheme(key, themePath(key));
        it = m_themes.insert(key, loaded);
    }
    return *it.value();
}

const NppTheme& StyleManager::currentTheme()
{ return theme(m_currentTheme); }

void StyleManager::setCurrentTheme(const QString& name)
{ m_currentTheme = name.isEmpty() ? QStringLiteral("default") : name; }

void StyleManager::applyLexer(const SciSender& sci, const Language* lang)
{
    Scintilla::ILexer5* lexer = nullptr;
    if (lang && !lang->lexer.isEmpty())
        lexer = CreateLexer(lang->lexer.toLatin1().constData());

    sci.send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lexer));
    if (!lexer)
        return;

    // Folding and lexer options, matching Notepad++ defaults.
    setProperty(sci, "fold", "1");
    setProperty(sci, "fold.compact", "0");
    setProperty(sci, "fold.comment", "1");
    setProperty(sci, "fold.preprocessor", "1");
    setProperty(sci, "fold.html", "1");
    setProperty(sci, "fold.html.preprocessor", "1");
    setProperty(sci, "fold.hypertext.comment", "1");
    setProperty(sci, "fold.hypertext.heredoc", "1");
    setProperty(sci, "fold.cpp.comment.explicit", "0");
    setProperty(sci, "fold.quotes.python", "1");
    setProperty(sci, "lexer.cpp.allow.dollars", "1");
    setProperty(sci, "lexer.python.decorator.attributes", "1");
    setProperty(sci, "lexer.json.escape.sequence", "1");

    const QString nppName = lang->nppName;
    if (nppName == QLatin1String("go") || nppName == QLatin1String("javascript.js") ||
        nppName == QLatin1String("typescript")) {
        setProperty(sci, "lexer.cpp.backquoted.strings", "1");
    }
    if (nppName == QLatin1String("json5"))
        setProperty(sci, "lexer.json.allow.comments", "1");
    if (nppName == QLatin1String("asp"))
        setProperty(sci, "asp.default.language", "2");

    // Keyword lists: langs.xml plus the extra keywords a theme may attach to a style class.
    const NppTheme& th = currentTheme();
    auto keywordsOf = [&](const QString& langName, const QString& cls) {
        QString words;
        if (const NppLangData* data = nppLanguage(langName))
            words = data->keywords.value(cls);
        for (const NppStyle& s : th.lexerStyles.value(langName)) {
            if (s.keywordClass == cls && !s.keywords.isEmpty())
                words += QLatin1Char(' ') + s.keywords;
        }
        return words.trimmed().toUtf8();
    };

    const QString& lexerName = lang->lexer;
    if (lexerName == QLatin1String("hypertext") || lexerName == QLatin1String("phpscript")) {
        sci.sendStr(SCI_SETKEYWORDS, 0, keywordsOf(QStringLiteral("html"), QStringLiteral("instre1")));
        sci.sendStr(SCI_SETKEYWORDS, 1, keywordsOf(QStringLiteral("javascript"), QStringLiteral("instre1")));
        sci.sendStr(SCI_SETKEYWORDS,
            2,
            keywordsOf(nppName == QLatin1String("asp") ? nppName : QStringLiteral("vb"), QStringLiteral("instre1")));
        sci.sendStr(SCI_SETKEYWORDS, 3, keywordsOf(QStringLiteral("python"), QStringLiteral("instre1")));
        sci.sendStr(SCI_SETKEYWORDS, 4, keywordsOf(QStringLiteral("php"), QStringLiteral("instre1")));
        sci.sendStr(SCI_SETKEYWORDS, 5, keywordsOf(QStringLiteral("html"), QStringLiteral("instre2")));
        return;
    }
    if (lexerName == QLatin1String("xml")) {
        sci.sendStr(SCI_SETKEYWORDS, 0, keywordsOf(nppName, QStringLiteral("instre1")));
        sci.sendStr(SCI_SETKEYWORDS, 5, keywordsOf(nppName, QStringLiteral("instre2")));
        return;
    }

    const QStringList& order = (lexerName == QLatin1String("cpp") || lexerName == QLatin1String("objc"))
                                   ? cppKeywordOrder()
                                   : genericKeywordOrder();
    for (int i = 0; i < order.size(); ++i)
        sci.sendStr(SCI_SETKEYWORDS, i, keywordsOf(nppName, order.at(i)));
}

void StyleManager::applyStyles(
    const SciSender& sci, const Language* lang, const QString& themeName, const EditorFont& font)
{
    const NppTheme& th = theme(themeName);
    const NppStyle def = th.global(QStringLiteral("Default Style"));

    const QString family =
        !font.family.isEmpty() ? font.family : (def.fontName.isEmpty() ? defaultMonospaceFamily() : def.fontName);
    const int size = font.size > 0 ? font.size : (def.fontSize > 0 ? def.fontSize : 10);

    sci.send(SCI_STYLERESETDEFAULT);
    sci.sendStr(SCI_STYLESETFONT, STYLE_DEFAULT, family.toUtf8());
    sci.send(SCI_STYLESETSIZE, STYLE_DEFAULT, size);
    sci.send(SCI_STYLESETFORE, STYLE_DEFAULT, th.defaultFg);
    sci.send(SCI_STYLESETBACK, STYLE_DEFAULT, th.defaultBg);
    if (def.fontStyle >= 0) {
        sci.send(SCI_STYLESETBOLD, STYLE_DEFAULT, (def.fontStyle & 1) != 0);
        sci.send(SCI_STYLESETITALIC, STYLE_DEFAULT, (def.fontStyle & 2) != 0);
    }
    sci.send(SCI_STYLECLEARALL);

    // Lexer specific styles. Font names/sizes from themes are ignored when the user overrides them.
    if (lang && !lang->nppName.isEmpty() && !lang->lexer.isEmpty()) {
        for (NppStyle s : th.lexerStyles.value(lang->nppName)) {
            if (!font.family.isEmpty())
                s.fontName.clear();
            if (font.size > 0)
                s.fontSize = -1;
            if (s.id == STYLE_DEFAULT)
                continue;
            applyOneStyle(sci, s.id, s);
        }
    }

    // Global styles that map to predefined Scintilla styles.
    for (const char* name :
        {"Line number margin", "Brace highlight style", "Bad brace colour", "Indent guideline style"}) {
        NppStyle s = th.global(QLatin1String(name));
        s.fontName.clear();
        s.fontSize = -1;
        applyOneStyle(sci, s.id, s);
    }

    // Element colours.
    const NppStyle caretLine = th.global(QStringLiteral("Current line background colour"));
    if (caretLine.bg >= 0) {
        sci.send(SCI_SETCARETLINEBACK, caretLine.bg);
        sci.send(SCI_SETCARETLINEVISIBLE, 1);
        sci.send(SCI_SETCARETLINEVISIBLEALWAYS, 1);
    }

    const NppStyle selection = th.global(QStringLiteral("Selected text colour"));
    if (selection.bg >= 0) {
        sci.send(SCI_SETSELBACK, 1, selection.bg);
        sci.send(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_BACK, selection.bg | 0xFF000000);
    }
    const NppStyle multiSelection = th.global(QStringLiteral("Multi-selected text color"));
    if (multiSelection.bg >= 0)
        sci.send(SCI_SETADDITIONALSELBACK, multiSelection.bg);

    const NppStyle caret = th.global(QStringLiteral("Caret colour"));
    if (caret.fg >= 0)
        sci.send(SCI_SETCARETFORE, caret.fg);
    const NppStyle multiCaret = th.global(QStringLiteral("Multi-edit carets color"));
    if (multiCaret.fg >= 0)
        sci.send(SCI_SETADDITIONALCARETFORE, multiCaret.fg);

    const NppStyle edge = th.global(QStringLiteral("Edge colour"));
    if (edge.fg >= 0)
        sci.send(SCI_SETEDGECOLOUR, edge.fg);

    const NppStyle ws = th.global(QStringLiteral("White space symbol"));
    if (ws.fg >= 0)
        sci.send(SCI_SETWHITESPACEFORE, 1, ws.fg);

    const NppStyle foldMargin = th.global(QStringLiteral("Fold margin"));
    if (foldMargin.bg >= 0) {
        sci.send(SCI_SETFOLDMARGINCOLOUR, 1, foldMargin.bg);
        sci.send(SCI_SETFOLDMARGINHICOLOUR, 1, foldMargin.fg >= 0 ? foldMargin.fg : foldMargin.bg);
    }

    const NppStyle fold = th.global(QStringLiteral("Fold"));
    const NppStyle foldActive = th.global(QStringLiteral("Fold active"));
    const int foldFg = fold.fg >= 0 ? fold.fg : 0x808080;
    const int foldBg = fold.bg >= 0 ? fold.bg : 0xF3F3F3;
    const int foldActiveFg = foldActive.fg >= 0 ? foldActive.fg : 0x0000FF;
    defineMarker(sci, SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS, foldBg, foldFg, foldActiveFg);
    defineMarker(sci, SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS, foldBg, foldFg, foldActiveFg);
    defineMarker(sci, SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE, foldBg, foldFg, foldActiveFg);
    defineMarker(sci, SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER, foldBg, foldFg, foldActiveFg);
    defineMarker(sci, SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED, foldBg, foldFg, foldActiveFg);
    defineMarker(sci, SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED, foldBg, foldFg, foldActiveFg);
    defineMarker(sci, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER, foldBg, foldFg, foldActiveFg);
    sci.send(SCI_MARKERENABLEHIGHLIGHT, 1);

    // Indicator used to highlight the other occurrences of the selected word.
    const NppStyle smart = th.global(QStringLiteral("Smart Highlighting"));
    const int smartIndicator = 29; // SCE_UNIVERSAL_FOUND_STYLE_SMART in Notepad++
    sci.send(SCI_INDICSETSTYLE, smartIndicator, INDIC_ROUNDBOX);
    sci.send(SCI_INDICSETFORE, smartIndicator, smart.bg >= 0 ? smart.bg : 0x00FF00);
    sci.send(SCI_INDICSETALPHA, smartIndicator, 100);
    sci.send(SCI_INDICSETOUTLINEALPHA, smartIndicator, 150);
    sci.send(SCI_INDICSETUNDER, smartIndicator, 1);

    // Line spacing.
    int extra = 0;
    if (font.lineHeight > 1.0)
        extra = qRound((font.lineHeight - 1.0) * size * 96.0 / 72.0 / 2.0);
    sci.send(SCI_SETEXTRAASCENT, extra);
    sci.send(SCI_SETEXTRADESCENT, extra);
}

} // namespace EditorNS
