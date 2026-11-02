#include "include/EditorNS/languageservice.h"

#include "include/EditorNS/nppdefs.h"
#include "include/EditorNS/stylemanager.h"
#include "include/notepadqq.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

namespace EditorNS {

namespace {

/**
 * Maps Notepadqq language ids (Languages.json) to the Notepad++ language providing the
 * lexer, keywords and theme styles. Languages missing here are shown as plain text.
 */
QString nppNameForLanguage(const QString& id, const QString& mode)
{
    static const QHash<QString, QString> byId = {
        {"plaintext", "normal"},
        {"asn.1", "asn1"},
        {"c", "c"},
        {"cpp", "cpp"},
        {"cobol", "cobol"},
        {"java", "java"},
        {"csharp", "cs"},
        {"scala", "java"},
        {"kotlin", "java"},
        {"groovy", "java"},
        {"haxe", "java"},
        {"dart", "java"},
        {"squirrel", "cpp"},
        {"protobuf", "cpp"},
        {"idl", "cpp"},
        {"web_idl", "cpp"},
        {"objective_c", "objc"},
        {"clojure", "lisp"},
        {"clojurescript", "lisp"},
        {"commonlisp", "lisp"},
        {"edn", "lisp"},
        {"closurestylesheets", "css"},
        {"less", "css"},
        {"scss", "css"},
        {"sass", "css"},
        {"css", "css"},
        {"cmake", "cmake"},
        {"coffeescript", "coffeescript"},
        {"cython", "python"},
        {"python", "python"},
        {"d", "d"},
        {"diff", "diff"},
        {"django", "html"},
        {"jinja2", "html"},
        {"twig", "html"},
        {"tornado", "html"},
        {"smarty", "html"},
        {"velocity", "html"},
        {"erb", "html"},
        {"ejs", "html"},
        {"html", "html"},
        {"aspnet", "asp"},
        {"jsp", "jsp"},
        {"dockerfile", "bash"},
        {"bash", "bash"},
        {"dtd", "xml"},
        {"xml", "xml"},
        {"erlang", "erlang"},
        {"forth", "forth"},
        {"fortran", "fortran"},
        {"fsharp", "caml"},
        {"ocaml", "caml"},
        {"gas", "asm"},
        {"z80", "asm"},
        {"go", "go"},
        {"haskell", "haskell"},
        {"haskellliterate", "haskell"},
        {"javascript", "javascript.js"},
        {"jsx", "javascript.js"},
        {"json", "json"},
        {"jsonld", "json"},
        {"typescript", "typescript"},
        {"lua", "lua"},
        {"makefile", "makefile"},
        {"mariadb", "sql"},
        {"plsql", "sql"},
        {"sql", "sql"},
        {"nsis", "nsis"},
        {"octave", "matlab"},
        {"pascal", "pascal"},
        {"perl", "perl"},
        {"php", "php"},
        {"powershell", "powershell"},
        {"properties", "props"},
        {"r", "r"},
        {"ruby", "ruby"},
        {"rust", "rust"},
        {"sas", "sas"},
        {"scheme", "scheme"},
        {"smalltalk", "smalltalk"},
        {"swift", "swift"},
        {"stex", "latex"},
        {"latex", "latex"},
        {"systemverilog", "verilog"},
        {"verilog", "verilog"},
        {"tcl", "tcl"},
        {"toml", "toml"},
        {"vb", "vb"},
        {"vbscript", "vb"},
        {"vhdl", "vhdl"},
        {"yaml", "yaml"},
    };
    auto it = byId.constFind(id);
    if (it != byId.constEnd())
        return it.value();
    if (mode == QLatin1String("clike"))
        return QStringLiteral("cpp");
    return QString();
}

void applyNppDefinition(Language& lang, const QString& nppName)
{
    const NppLangDef* def = nppLangDefByName(nppName);
    if (!def)
        return;
    lang.nppName = nppName;
    lang.lexer = QString::fromLatin1(def->lexer);
    lang.langType = def->langType;
    if (const NppLangData* data = StyleManager::instance().nppLanguage(nppName)) {
        lang.commentLine = data->commentLine;
        lang.commentStart = data->commentStart;
        lang.commentEnd = data->commentEnd;
    }
}

} // namespace

LanguageService::LanguageService()
{
    QFile scriptFile(Notepadqq::languagesPath());
    QJsonObject root;
    if (scriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        root = QJsonDocument::fromJson(scriptFile.readAll()).object();
        scriptFile.close();
    }

    // Reserve the space we need to prevent resize and copy constructs:
    // lookups hand out pointers into m_languages.
    m_languages.reserve(root.count() + static_cast<int>(nppLanguages().size()) + 1);

    QSet<QString> usedNppNames;
    bool hasPlainText = false;
    // Begin iterating our QJsonDocument's object and adding languages.
    for (auto&& key : root.keys()) {
        if (key == "plaintext")
            hasPlainText = true;
        auto mode = root.value(key).toObject();
        Language newMode;
        newMode.id = key;
        newMode.name = mode.value("name").toString();
        newMode.mime = mode.value("mime").toString();
        newMode.mode = mode.value("mode").toString();
        newMode.fileNames = mode.value("fileNames").toVariant().toStringList();
        newMode.fileExtensions = mode.value("fileExtensions").toVariant().toStringList();
        newMode.firstNonBlankLine = mode.value("firstNonBlankLine").toVariant().toStringList();
        const QString nppName = nppNameForLanguage(key, newMode.mode);
        applyNppDefinition(newMode, nppName);
        usedNppNames.insert(nppName);
        m_languages.append(std::move(newMode));
    }

    if (!hasPlainText) {
        Language plain;
        plain.id = "plaintext";
        plain.name = "Text";
        plain.mime = "text/plain";
        plain.mode = "null";
        plain.fileExtensions = QStringList{"txt"};
        applyNppDefinition(plain, "normal");
        m_languages.append(std::move(plain));
    }

    // Notepad++ languages that Notepadqq did not know about (Batch, INI, AutoIt, Inno Setup, ...).
    for (const NppLangDef& def : nppLanguages()) {
        const QString nppName = QString::fromLatin1(def.name);
        if (usedNppNames.contains(nppName) || def.langType == NppLangType::L_USER ||
            def.langType == NppLangType::L_SEARCHRESULT || nppName == QLatin1String("javascript")) {
            continue;
        }
        if (lookupById(nppName) != nullptr)
            continue;
        Language lang;
        lang.id = nppName;
        lang.name = QString::fromUtf8(def.displayName);
        lang.mode = nppName;
        if (const NppLangData* data = StyleManager::instance().nppLanguage(nppName))
            lang.fileExtensions = data->extensions;
        applyNppDefinition(lang, nppName);
        m_languages.append(std::move(lang));
    }
}

const Language* LanguageService::lookupByNppLangType(int langType)
{
    // Prefer the language whose own Notepad++ name matches the LangType (e.g. "cpp" over "squirrel").
    const NppLangDef* def = nppLangDefByType(langType);
    if (!def)
        return nullptr;
    const QString nppName = QString::fromLatin1(def->name);
    const Language* fallback = nullptr;
    for (const Language& l : m_languages) {
        if (l.nppName != nppName)
            continue;
        if (l.id == nppName || (nppName == QLatin1String("normal") && l.id == QLatin1String("plaintext")))
            return &l;
        if (!fallback)
            fallback = &l;
    }
    return fallback;
}

const Language* LanguageService::lookupById(const QString& id)
{
    auto it = std::find_if(m_languages.begin(), m_languages.end(), [&id](const Language& l) { return (l.id == id); });
    if (it == m_languages.end())
        return nullptr;
    return &(*it);
}

const Language* LanguageService::lookupByFileName(const QString& fileName)
{
    auto it = std::find_if(m_languages.begin(), m_languages.end(), [&fileName](const Language& l) {
        return (l.fileNames.contains(fileName));
    });
    if (it == m_languages.end())
        return nullptr;
    return &(*it);
}

const Language* LanguageService::lookupByExtension(const QString& fileName)
{
    QFileInfo fi(fileName);
    auto ext = fi.suffix();
    auto it = std::find_if(m_languages.begin(), m_languages.end(), [&ext](const Language& l) {
        return (l.fileExtensions.contains(ext, Qt::CaseInsensitive));
    });
    if (it == m_languages.end())
        return nullptr;
    return &(*it);
}

const Language* LanguageService::lookupByContent(QString content)
{
    if (content.isEmpty()) {
        return nullptr;
    }
    QTextStream stream(&content);
    stream.skipWhiteSpace();
    auto test = stream.readLine();
    for (auto&& l : m_languages) {
        if (l.firstNonBlankLine.isEmpty())
            continue;
        for (auto&& t : l.firstNonBlankLine) {
            if (test.contains(QRegularExpression(t))) {
                return &l;
            }
        }
    }
    return nullptr;
}

LanguageService& LanguageService::getInstance()
{
    static LanguageService instance;
    return instance;
}

} // namespace EditorNS
