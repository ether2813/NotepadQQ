#include "include/EditorNS/nppdefs.h"

namespace EditorNS {

// Mirrors Notepad++'s `enum LangType` and its built-in lexer table
// (PowerEditor/src/ScintillaComponent/ScintillaEditView.cpp, _langNameInfoArray).
const std::vector<NppLangDef>& nppLanguages()
{
    static const std::vector<NppLangDef> langs = {
        {"normal", "", 0, "Normal text"},
        {"php", "phpscript", 1, "PHP"},
        {"c", "cpp", 2, "C"},
        {"cpp", "cpp", 3, "C++"},
        {"cs", "cpp", 4, "C#"},
        {"objc", "objc", 5, "Objective-C"},
        {"java", "cpp", 6, "Java"},
        {"rc", "cpp", 7, "RC"},
        {"html", "hypertext", 8, "HTML"},
        {"xml", "xml", 9, "XML"},
        {"makefile", "makefile", 10, "Makefile"},
        {"pascal", "pascal", 11, "Pascal"},
        {"batch", "batch", 12, "Batch"},
        {"ini", "props", 13, "ini"},
        {"nfo", "", 14, "NFO"},
        {"udf", "user", 15, "User Defined"},
        {"asp", "hypertext", 16, "ASP"},
        {"sql", "sql", 17, "SQL"},
        {"vb", "vb", 18, "Visual Basic"},
        {"javascript", "cpp", 19, "JavaScript (embedded)"},
        {"css", "css", 20, "CSS"},
        {"perl", "perl", 21, "Perl"},
        {"python", "python", 22, "Python"},
        {"lua", "lua", 23, "Lua"},
        {"tex", "tex", 24, "TeX"},
        {"fortran", "fortran", 25, "Fortran free form"},
        {"bash", "bash", 26, "Shell"},
        {"actionscript", "cpp", 27, "ActionScript"},
        {"nsis", "nsis", 28, "NSIS"},
        {"tcl", "tcl", 29, "TCL"},
        {"lisp", "lisp", 30, "Lisp"},
        {"scheme", "lisp", 31, "Scheme"},
        {"asm", "asm", 32, "Assembly"},
        {"diff", "diff", 33, "Diff"},
        {"props", "props", 34, "Properties file"},
        {"postscript", "ps", 35, "PostScript"},
        {"ruby", "ruby", 36, "Ruby"},
        {"smalltalk", "smalltalk", 37, "Smalltalk"},
        {"vhdl", "vhdl", 38, "VHDL"},
        {"kix", "kix", 39, "KiXtart"},
        {"autoit", "au3", 40, "AutoIt"},
        {"caml", "caml", 41, "CAML"},
        {"ada", "ada", 42, "Ada"},
        {"verilog", "verilog", 43, "Verilog"},
        {"matlab", "matlab", 44, "MATLAB"},
        {"haskell", "haskell", 45, "Haskell"},
        {"inno", "inno", 46, "Inno Setup"},
        {"searchResult", "searchResult", 47, "Internal Search"},
        {"cmake", "cmake", 48, "CMake"},
        {"yaml", "yaml", 49, "YAML"},
        {"cobol", "COBOL", 50, "COBOL"},
        {"gui4cli", "gui4cli", 51, "Gui4Cli"},
        {"d", "d", 52, "D"},
        {"powershell", "powershell", 53, "PowerShell"},
        {"r", "r", 54, "R"},
        {"jsp", "hypertext", 55, "JSP"},
        {"coffeescript", "coffeescript", 56, "CoffeeScript"},
        {"json", "json", 57, "JSON"},
        {"javascript.js", "cpp", 58, "JavaScript"},
        {"fortran77", "f77", 59, "Fortran fixed form"},
        {"baanc", "baan", 60, "BaanC"},
        {"srec", "srec", 61, "S-Record"},
        {"ihex", "ihex", 62, "Intel HEX"},
        {"tehex", "tehex", 63, "Tektronix extended HEX"},
        {"swift", "cpp", 64, "Swift"},
        {"asn1", "asn1", 65, "ASN.1"},
        {"avs", "avs", 66, "AviSynth"},
        {"blitzbasic", "blitzbasic", 67, "BlitzBasic"},
        {"purebasic", "purebasic", 68, "PureBasic"},
        {"freebasic", "freebasic", 69, "FreeBasic"},
        {"csound", "csound", 70, "Csound"},
        {"erlang", "erlang", 71, "Erlang"},
        {"escript", "escript", 72, "ESCRIPT"},
        {"forth", "forth", 73, "Forth"},
        {"latex", "latex", 74, "LaTeX"},
        {"mmixal", "mmixal", 75, "MMIXAL"},
        {"nim", "nim", 76, "Nim"},
        {"nncrontab", "nncrontab", 77, "Nncrontab"},
        {"oscript", "oscript", 78, "OScript"},
        {"rebol", "rebol", 79, "REBOL"},
        {"registry", "registry", 80, "Registry"},
        {"rust", "rust", 81, "Rust"},
        {"spice", "spice", 82, "Spice"},
        {"txt2tags", "txt2tags", 83, "txt2tags"},
        {"visualprolog", "visualprolog", 84, "Visual Prolog"},
        {"typescript", "cpp", 85, "TypeScript"},
        {"json5", "json", 86, "JSON5"},
        {"mssql", "mssql", 87, "MS SQL"},
        {"gdscript", "gdscript", 88, "GDScript"},
        {"hollywood", "hollywood", 89, "Hollywood"},
        {"go", "cpp", 90, "Go"},
        {"raku", "raku", 91, "Raku"},
        {"toml", "toml", 92, "TOML"},
        {"sas", "sas", 93, "SAS"},
        {"errorlist", "errorlist", 94, "ErrorList"},
        {"escseq", "escseq", 95, "Escape Sequences"},
    };
    return langs;
}

const NppLangDef* nppLangDefByName(const QString& name)
{
    for (const NppLangDef& def : nppLanguages()) {
        if (name == QLatin1String(def.name))
            return &def;
    }
    return nullptr;
}

const NppLangDef* nppLangDefByType(int langType)
{
    for (const NppLangDef& def : nppLanguages()) {
        if (def.langType == langType)
            return &def;
    }
    return nullptr;
}

} // namespace EditorNS
