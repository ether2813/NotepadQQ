#ifndef NQQ_NPPDEFS_H
#define NQQ_NPPDEFS_H

#include <QString>

#include <vector>

namespace EditorNS {

/**
 * @brief Static description of a Notepad++ built-in language.
 *
 * The names match the <Language name="..."> entries of Notepad++'s langs.xml and the
 * <LexerType name="..."> entries of stylers.xml / themes, so the original Notepad++ data
 * files (and third party Notepad++ themes) can be used unchanged. langType is the value of
 * Notepad++'s `enum LangType` that plugins receive through NPPM_GETCURRENTLANGTYPE.
 */
struct NppLangDef {
    const char* name;
    const char* lexer;
    int langType;
    const char* displayName;
};

/** All built-in Notepad++ languages, in LangType order. */
const std::vector<NppLangDef>& nppLanguages();

/** Returns nullptr if no Notepad++ language has this name. */
const NppLangDef* nppLangDefByName(const QString& name);

/** Returns nullptr if the value is not a known LangType. */
const NppLangDef* nppLangDefByType(int langType);

/** LangType values that are referenced from C++ code. */
namespace NppLangType {
constexpr int L_TEXT = 0;
constexpr int L_USER = 15;
constexpr int L_SEARCHRESULT = 47;
constexpr int L_EXTERNAL = 96;
} // namespace NppLangType

} // namespace EditorNS

#endif // NQQ_NPPDEFS_H
