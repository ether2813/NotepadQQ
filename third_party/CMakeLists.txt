# Native editing component used by Notepadqq.
#
# Scintilla and Lexilla are vendored from the Notepad++ source tree (see
# scintilla/version.txt and lexilla/version.txt) so that Notepad++ plugins see
# exactly the same Scintilla API, message numbers and lexers they were built
# against.
#
#   * On Windows the real Win32 Scintilla control is built. Every editor view is a
#     native "Scintilla" HWND, which is what Notepad++ plugins talk to via
#     SendMessage / SCI_GETDIRECTFUNCTION.
#   * On other platforms the official Qt port (ScintillaEditBase) is built so the
#     same editor code can be developed and tested on Linux and macOS.

set(NQQ_SCINTILLA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scintilla")
set(NQQ_LEXILLA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lexilla")

# ---------------------------------------------------------------- Lexilla
file(GLOB NQQ_LEXILLA_LEXERS CONFIGURE_DEPENDS "${NQQ_LEXILLA_DIR}/lexers/*.cxx")
file(GLOB NQQ_LEXILLA_LEXLIB CONFIGURE_DEPENDS "${NQQ_LEXILLA_DIR}/lexlib/*.cxx")

add_library(nqq_lexilla STATIC
    ${NQQ_LEXILLA_LEXERS}
    ${NQQ_LEXILLA_LEXLIB}
    "${NQQ_LEXILLA_DIR}/src/Lexilla.cxx"
)
target_include_directories(nqq_lexilla
    PUBLIC
        "${NQQ_LEXILLA_DIR}/include"
        "${NQQ_SCINTILLA_DIR}/include"
    PRIVATE
        "${NQQ_LEXILLA_DIR}/lexlib"
)
target_compile_features(nqq_lexilla PUBLIC cxx_std_17)
set_target_properties(nqq_lexilla PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(nqq_lexilla PRIVATE _CRT_SECURE_NO_DEPRECATE _SCL_SECURE_NO_WARNINGS)
    target_compile_options(nqq_lexilla PRIVATE /W0 /utf-8)
else()
    target_compile_options(nqq_lexilla PRIVATE -w)
endif()

# ---------------------------------------------------------------- Scintilla
file(GLOB NQQ_SCINTILLA_CORE CONFIGURE_DEPENDS "${NQQ_SCINTILLA_DIR}/src/*.cxx")

if(WIN32)
    add_library(nqq_scintilla STATIC
        ${NQQ_SCINTILLA_CORE}
        "${NQQ_SCINTILLA_DIR}/win32/HanjaDic.cxx"
        "${NQQ_SCINTILLA_DIR}/win32/ListBox.cxx"
        "${NQQ_SCINTILLA_DIR}/win32/PlatWin.cxx"
        "${NQQ_SCINTILLA_DIR}/win32/ScintillaWin.cxx"
        "${NQQ_SCINTILLA_DIR}/win32/SurfaceD2D.cxx"
        "${NQQ_SCINTILLA_DIR}/win32/SurfaceGDI.cxx"
    )
    target_include_directories(nqq_scintilla
        PUBLIC "${NQQ_SCINTILLA_DIR}/include"
        PRIVATE "${NQQ_SCINTILLA_DIR}/src" "${NQQ_SCINTILLA_DIR}/win32"
    )
    target_compile_definitions(nqq_scintilla PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(nqq_scintilla PUBLIC imm32 msimg32 ole32 oleaut32 uuid gdi32 user32 comctl32)
    set_target_properties(nqq_scintilla PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
else()
    add_library(nqq_scintilla STATIC
        ${NQQ_SCINTILLA_CORE}
        "${NQQ_SCINTILLA_DIR}/qt/ScintillaEditBase/PlatQt.cpp"
        "${NQQ_SCINTILLA_DIR}/qt/ScintillaEditBase/ScintillaEditBase.cpp"
        "${NQQ_SCINTILLA_DIR}/qt/ScintillaEditBase/ScintillaQt.cpp"
        "${NQQ_SCINTILLA_DIR}/qt/ScintillaEditBase/ScintillaEditBase.h"
        "${NQQ_SCINTILLA_DIR}/qt/ScintillaEditBase/ScintillaQt.h"
    )
    target_include_directories(nqq_scintilla
        PUBLIC "${NQQ_SCINTILLA_DIR}/include" "${NQQ_SCINTILLA_DIR}/qt/ScintillaEditBase" "${NQQ_SCINTILLA_DIR}/src"
    )
    target_compile_definitions(nqq_scintilla PUBLIC SCINTILLA_QT=1 MAKING_LIBRARY=1 SCI_LEXER=1 _CRT_SECURE_NO_DEPRECATE=1)
    target_link_libraries(nqq_scintilla PUBLIC Qt6::Core Qt6::Core5Compat Qt6::Gui Qt6::Widgets)
    set_target_properties(nqq_scintilla PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_features(nqq_scintilla PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(nqq_scintilla PRIVATE /W0 /utf-8)
else()
    target_compile_options(nqq_scintilla PRIVATE -w)
endif()

# Vendored code is not subject to the project's static analysis.
set_target_properties(nqq_lexilla nqq_scintilla PROPERTIES CXX_CLANG_TIDY "")
