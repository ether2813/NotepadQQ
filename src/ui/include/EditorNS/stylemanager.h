#ifndef NQQ_STYLEMANAGER_H
#define NQQ_STYLEMANAGER_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace EditorNS {

class SciSender;
struct Language;

/** One <WordsStyle>/<WidgetStyle> entry of a Notepad++ stylers/theme file. */
struct NppStyle {
    int id = -1;
    QString name;
    int fg = -1; ///< COLORREF (0x00BBGGRR) or -1 if unset
    int bg = -1; ///< COLORREF (0x00BBGGRR) or -1 if unset
    QString fontName;
    int fontStyle = -1; ///< bit 1 bold, bit 2 italic, bit 4 underline, -1 if unset
    int fontSize = -1;
    QString keywordClass;
    QString keywords;
};

/** A parsed Notepad++ theme (stylers.xml format). */
struct NppTheme {
    QString name;
    QString path;
    QHash<QString, QVector<NppStyle>> lexerStyles; ///< keyed by Notepad++ language name
    QHash<QString, NppStyle> globalStyles;         ///< keyed by WidgetStyle name
    int defaultFg = 0x000000;
    int defaultBg = 0xFFFFFF;
    bool isDark() const;
    NppStyle global(const QString& name) const { return globalStyles.value(name); }
};

/** A parsed <Language> entry of Notepad++'s langs.xml. */
struct NppLangData {
    QString name;
    QStringList extensions;
    QString commentLine;
    QString commentStart;
    QString commentEnd;
    QHash<QString, QString> keywords; ///< keyed by class (instre1, type1, ...)
};

/** Font overrides coming from Notepadqq's appearance settings. */
struct EditorFont {
    QString family; ///< empty means "use the theme font"
    int size = 0;   ///< 0 means "use the theme size"
    double lineHeight = 0;
};

/**
 * @brief Loads Notepad++ language definitions (langs.xml) and themes (stylers.xml format)
 *        and applies them to Scintilla views and documents.
 *
 * Lexers and keywords belong to a Scintilla *document* (applyLexer), while colours and fonts
 * belong to a Scintilla *view* (applyStyles). This mirrors Notepad++, which re-applies the
 * styles every time a buffer is activated in one of its two views.
 */
class StyleManager {
public:
    static StyleManager& instance();

    /** "default" followed by every theme found in the bundled and user theme folders. */
    QStringList themeNames();
    const NppTheme& theme(const QString& name);
    const NppTheme& currentTheme();
    QString currentThemeName() const { return m_currentTheme; }
    void setCurrentTheme(const QString& name);

    const NppLangData* nppLanguage(const QString& nppName);

    /** Sets lexer, lexer properties and keyword lists on the document shown by @p sci. */
    void applyLexer(const SciSender& sci, const Language* lang);

    /** Applies theme colours, fonts, margins and markers to the view @p sci. */
    void applyStyles(const SciSender& sci, const Language* lang, const QString& themeName, const EditorFont& font);

    /** Directory containing langs.xml, stylers.xml and themes/. */
    static QString dataPath();
    /** Per-user directory where additional Notepad++ themes can be dropped. */
    static QString userThemesPath();

    static int colorFromHex(const QString& rrggbb);

private:
    StyleManager();
    void loadLanguages();
    std::unique_ptr<NppTheme> loadTheme(const QString& name, const QString& path) const;
    QString themePath(const QString& name);

    bool m_languagesLoaded = false;
    QHash<QString, NppLangData> m_languages;
    QHash<QString, std::shared_ptr<NppTheme>> m_themes;
    QString m_currentTheme = QStringLiteral("default");
};

} // namespace EditorNS

#endif // NQQ_STYLEMANAGER_H
