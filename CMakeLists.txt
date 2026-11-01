cmake_minimum_required(VERSION 3.24)

project(notepadqq VERSION 2.2.0 LANGUAGES CXX)

include(GNUInstallDirs)

option(NQQ_BUILD_TESTS "Build Notepadqq tests" ON)
option(NQQ_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(NQQ_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(NQQ_BUILD_SNAP "Build Notepadqq for Snap packaging" OFF)
option(NQQ_ENABLE_NPP_PLUGINS "Enable the Notepad++ plugin host (Windows only)" ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Core5Compat
    Gui
    LinguistTools
    Network
    PrintSupport
    Svg
    Test
    WebSockets
    Widgets
)

# Character set detection: uchardet on Linux/macOS, the built-in MLang service on Windows.
set(NQQ_UCHARDET_TARGET "")
if(NOT WIN32)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(UCHARDET IMPORTED_TARGET uchardet)
    endif()
    if(UCHARDET_FOUND)
        set(NQQ_UCHARDET_TARGET PkgConfig::UCHARDET)
    else()
        message(WARNING "uchardet not found: only UTF-8 files will be auto-detected")
    endif()
endif()

if(MSVC)
    # Debug information for Release builds (embedded in the objects, PDB next to the binaries)
    # so that crash reports and the test crash handler show symbolic stack traces.
    add_compile_options($<$<CONFIG:Release>:/Z7>)
    add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

# Native editing component (Scintilla + Lexilla, see third_party/CMakeLists.txt).
add_subdirectory(third_party)

qt_standard_project_setup(REQUIRES 6.4)

set(NQQ_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(NQQ_UI_DIR "${NQQ_ROOT_DIR}/src/ui")
set(NQQ_TRANSLATIONS_DIR "${NQQ_ROOT_DIR}/src/translations")
set(NQQ_SUPPORT_DIR "${NQQ_ROOT_DIR}/support_files")
set(NQQ_STAGED_APPDATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/appdata")

set(NQQ_APP_SOURCES
    src/ui/main.cpp
    src/ui/mainwindow.cpp
    src/ui/documentcontroller.cpp
    src/ui/editoruicontroller.cpp
    src/ui/windowuicontroller.cpp
    src/ui/topeditorcontainer.cpp
    src/ui/editortabwidget.cpp
    src/ui/docengine.cpp
    src/ui/frmabout.cpp
    src/ui/notepadqq.cpp
    src/ui/frmpreferences.cpp
    src/ui/iconprovider.cpp
    src/ui/EditorNS/editor.cpp
    src/ui/EditorNS/asyncrequesttracker.cpp
    src/ui/EditorNS/bannerfilechanged.cpp
    src/ui/EditorNS/bannerbasicmessage.cpp
    src/ui/EditorNS/bannerfileremoved.cpp
    src/ui/EditorNS/languageservice.cpp
    src/ui/EditorNS/nppdefs.cpp
    src/ui/EditorNS/scintillaview.cpp
    src/ui/EditorNS/stylemanager.cpp
    src/ui/encodingdetector.cpp
    src/ui/clickablelabel.cpp
    src/ui/frmencodingchooser.cpp
    src/ui/EditorNS/bannerindentationdetected.cpp
    src/ui/frmindentationmode.cpp
    src/ui/singleapplication.cpp
    src/ui/localcommunication.cpp
    src/ui/Search/frmsearchreplace.cpp
    src/ui/Search/searchstring.cpp
    src/ui/Search/searchhelpers.cpp
    src/ui/Search/advancedsearchdock.cpp
    src/ui/Extensions/extension.cpp
    src/ui/frmlinenumberchooser.cpp
    src/ui/Extensions/extensionsserver.cpp
    src/ui/Extensions/Stubs/stub.cpp
    src/ui/Extensions/runtimesupport.cpp
    src/ui/Extensions/Stubs/windowstub.cpp
    src/ui/Extensions/Stubs/notepadqqstub.cpp
    src/ui/Extensions/Stubs/editorstub.cpp
    src/ui/Extensions/extensionsloader.cpp
    src/ui/globals.cpp
    src/ui/Extensions/Stubs/menuitemstub.cpp
    src/ui/Extensions/installextension.cpp
    src/ui/keygrabber.cpp
    src/ui/Sessions/sessions.cpp
    src/ui/Sessions/persistentcache.cpp
    src/ui/nqqsettings.cpp
    src/ui/nqqrun.cpp
    src/ui/Search/filesearcher.cpp
    src/ui/Search/filereplacer.cpp
    src/ui/Search/searchobjects.cpp
    src/ui/Search/searchinstance.cpp
    src/ui/stats.cpp
    src/ui/statsruntime.cpp
    src/ui/localsockethelpers.cpp
    src/ui/commandlineopenruntime.cpp
    src/ui/Sessions/backupservice.cpp
    src/ui/svgiconengine.cpp
)

if(WIN32)
    # Keyboard integration for native Scintilla windows.
    list(APPEND NQQ_APP_SOURCES src/ui/EditorNS/nativekeyrouter.cpp)
endif()
if(WIN32 AND NQQ_ENABLE_NPP_PLUGINS)
    list(APPEND NQQ_APP_SOURCES
        src/ui/NppPlugins/npppluginmanager.cpp
        src/ui/NppPlugins/nppmessagehandler.cpp
        src/ui/NppPlugins/nppdocking.cpp
    )
endif()

set(NQQ_APP_HEADERS
    src/ui/include/mainwindow.h
    src/ui/include/documentcontroller.h
    src/ui/include/editoruicontroller.h
    src/ui/include/windowuicontroller.h
    src/ui/include/topeditorcontainer.h
    src/ui/include/editortabwidget.h
    src/ui/include/docengine.h
    src/ui/include/frmabout.h
    src/ui/include/notepadqq.h
    src/ui/include/frmpreferences.h
    src/ui/include/iconprovider.h
    src/ui/include/EditorNS/editor.h
    src/ui/include/EditorNS/asyncrequesttracker.h
    src/ui/include/EditorNS/bannerfilechanged.h
    src/ui/include/EditorNS/bannerbasicmessage.h
    src/ui/include/EditorNS/bannerfileremoved.h
    src/ui/include/EditorNS/nppdefs.h
    src/ui/include/EditorNS/scintillaview.h
    src/ui/include/EditorNS/stylemanager.h
    src/ui/include/encodingdetector.h
    src/ui/include/clickablelabel.h
    src/ui/include/frmencodingchooser.h
    src/ui/include/EditorNS/bannerindentationdetected.h
    src/ui/include/EditorNS/languageservice.h
    src/ui/include/frmindentationmode.h
    src/ui/include/singleapplication.h
    src/ui/include/localcommunication.h
    src/ui/include/Search/frmsearchreplace.h
    src/ui/include/Search/advancedsearchdock.h
    src/ui/include/Search/searchhelpers.h
    src/ui/include/Search/searchstring.h
    src/ui/include/Extensions/extension.h
    src/ui/include/frmlinenumberchooser.h
    src/ui/include/Extensions/extensionsserver.h
    src/ui/include/Extensions/Stubs/stub.h
    src/ui/include/Extensions/runtimesupport.h
    src/ui/include/Extensions/Stubs/windowstub.h
    src/ui/include/Extensions/Stubs/notepadqqstub.h
    src/ui/include/Extensions/Stubs/editorstub.h
    src/ui/include/Extensions/extensionsloader.h
    src/ui/include/globals.h
    src/ui/include/Extensions/Stubs/menuitemstub.h
    src/ui/include/Extensions/installextension.h
    src/ui/include/keygrabber.h
    src/ui/include/Sessions/sessions.h
    src/ui/include/Sessions/persistentcache.h
    src/ui/include/nqqsettings.h
    src/ui/include/nqqrun.h
    src/ui/include/Search/filesearcher.h
    src/ui/include/Search/searchobjects.h
    src/ui/include/Search/filereplacer.h
    src/ui/include/Search/searchinstance.h
    src/ui/include/stats.h
    src/ui/include/statsruntime.h
    src/ui/include/localsockethelpers.h
    src/ui/include/commandlineopenruntime.h
    src/ui/include/Sessions/backupservice.h
    src/ui/include/svgiconengine.h
)

if(WIN32)
    list(APPEND NQQ_APP_HEADERS src/ui/include/EditorNS/nativekeyrouter.h)
endif()
if(WIN32 AND NQQ_ENABLE_NPP_PLUGINS)
    list(APPEND NQQ_APP_HEADERS
        src/ui/NppPlugins/npppluginmanager.h
        src/ui/NppPlugins/nppdocking.h
    )
endif()

# Settings shared by every target that compiles the Notepadqq sources.
function(nqq_configure_target target)
    target_include_directories(${target} PRIVATE
        src/ui
        src/ui/include
        src/ui/libs/qtpromise/include
        src/ui/libs/qtpromise/src
    )
    target_compile_definitions(${target} PRIVATE QT_NO_URL_CAST_FROM_STRING)
    if(NQQ_UCHARDET_TARGET)
        target_compile_definitions(${target} PRIVATE NQQ_HAVE_UCHARDET)
    endif()
    # Workaround for upstream qtpromise bug (v0.7.0): qpromise_p.h forward-declares
    # QPromiseConversionException but never includes qpromiseexceptions.h where it's
    # defined. This causes "invalid use of incomplete type" when thrown at line 593.
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /permissive- /Zc:__cplusplus /bigobj
            "/FIqtpromise/qpromiseexceptions.h")
        target_compile_definitions(${target} PRIVATE NOMINMAX UNICODE _UNICODE)
    else()
        target_compile_options(${target} PRIVATE -include qtpromise/qpromiseexceptions.h)
    endif()
    if(WIN32)
        target_compile_definitions(${target} PRIVATE NOMINMAX)
        if(NQQ_ENABLE_NPP_PLUGINS)
            target_compile_definitions(${target} PRIVATE NQQ_NPP_PLUGINS)
            target_include_directories(${target} PRIVATE src/ui/NppPlugins src/ui/NppPlugins/npp)
        endif()
    endif()
    target_link_libraries(${target} PRIVATE
        Qt6::Core
        Qt6::Core5Compat
        Qt6::Gui
        Qt6::Network
        Qt6::PrintSupport
        Qt6::Svg
        Qt6::WebSockets
        Qt6::Widgets
        nqq_scintilla
        nqq_lexilla
        ${NQQ_UCHARDET_TARGET}
    )
    if(WIN32)
        target_link_libraries(${target} PRIVATE user32 gdi32 ole32 oleaut32 uuid comctl32 shell32 shlwapi version)
    endif()
endfunction()

set(NQQ_APP_FORMS
    src/ui/mainwindow.ui
    src/ui/frmabout.ui
    src/ui/frmpreferences.ui
    src/ui/frmencodingchooser.ui
    src/ui/frmindentationmode.ui
    src/ui/Search/dlgsearching.ui
    src/ui/Search/frmsearchreplace.ui
    src/ui/frmlinenumberchooser.ui
    src/ui/Extensions/installextension.ui
)

set(NQQ_TS_FILES
    src/translations/notepadqq_de.ts
    src/translations/notepadqq_es.ts
    src/translations/notepadqq_fr.ts
    src/translations/notepadqq_hu.ts
    src/translations/notepadqq_it.ts
    src/translations/notepadqq_ja.ts
    src/translations/notepadqq_nl.ts
    src/translations/notepadqq_pl.ts
    src/translations/notepadqq_pt.ts
    src/translations/notepadqq_ru.ts
    src/translations/notepadqq_sl.ts
    src/translations/notepadqq_sv.ts
    src/translations/notepadqq_uk.ts
    src/translations/notepadqq_zh.ts
)

qt_add_translation(NQQ_QM_FILES ${NQQ_TS_FILES})

file(READ "${NQQ_UI_DIR}/resources.qrc" NQQ_ICONS_QRC)
string(FIND "${NQQ_ICONS_QRC}" "    <qresource prefix=\"/translations\">" NQQ_TRANSLATIONS_QRC_POS)
if(NQQ_TRANSLATIONS_QRC_POS GREATER -1)
    string(SUBSTRING "${NQQ_ICONS_QRC}" 0 ${NQQ_TRANSLATIONS_QRC_POS} NQQ_ICONS_QRC)
    string(APPEND NQQ_ICONS_QRC "</RCC>\n")
endif()
string(REPLACE ">icons/" ">${NQQ_UI_DIR}/icons/" NQQ_ICONS_QRC "${NQQ_ICONS_QRC}")
set(NQQ_GENERATED_ICONS_QRC "${CMAKE_CURRENT_BINARY_DIR}/notepadqq-icons.qrc")
file(WRITE "${NQQ_GENERATED_ICONS_QRC}" "${NQQ_ICONS_QRC}")

set(NQQ_TRANSLATIONS_QRC "${CMAKE_CURRENT_BINARY_DIR}/notepadqq-translations.qrc")
file(WRITE "${NQQ_TRANSLATIONS_QRC}" "<RCC>\n    <qresource prefix=\"/translations\">\n")
foreach(NQQ_QM_FILE IN LISTS NQQ_QM_FILES)
    get_filename_component(NQQ_QM_NAME "${NQQ_QM_FILE}" NAME)
    file(APPEND "${NQQ_TRANSLATIONS_QRC}" "        <file alias=\"${NQQ_QM_NAME}\">${NQQ_QM_FILE}</file>\n")
endforeach()
file(APPEND "${NQQ_TRANSLATIONS_QRC}" "    </qresource>\n</RCC>\n")

set(NQQ_TARGET_NAME notepadqq)

set(NQQ_WIN_RESOURCES "")
if(WIN32)
    set(NQQ_WIN_RESOURCES support_files/windows/notepadqq.rc)
endif()

qt_add_executable(${NQQ_TARGET_NAME} WIN32 MACOSX_BUNDLE
    ${NQQ_APP_SOURCES}
    ${NQQ_APP_HEADERS}
    ${NQQ_APP_FORMS}
    ${NQQ_GENERATED_ICONS_QRC}
    ${NQQ_TRANSLATIONS_QRC}
    ${NQQ_WIN_RESOURCES}
)

add_custom_target(notepadqq_translations DEPENDS ${NQQ_QM_FILES})
add_dependencies(${NQQ_TARGET_NAME} notepadqq_translations)

nqq_configure_target(${NQQ_TARGET_NAME})
if(MSVC)
    # The manifest is embedded by support_files/windows/notepadqq.rc.
    target_link_options(${NQQ_TARGET_NAME} PRIVATE /MANIFEST:NO)
endif()

if(NQQ_BUILD_SNAP)
    target_compile_definitions(${NQQ_TARGET_NAME} PRIVATE BUILD_SNAP)
endif()

set_target_properties(${NQQ_TARGET_NAME} PROPERTIES
    AUTOUIC_SEARCH_PATHS "${NQQ_UI_DIR};${NQQ_UI_DIR}/Search;${NQQ_UI_DIR}/Extensions"
)

target_compile_options(${NQQ_TARGET_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wold-style-cast>
)

if(APPLE)
    target_sources(${NQQ_TARGET_NAME} PRIVATE images/notepadqq.icns)
    set_source_files_properties(images/notepadqq.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(${NQQ_TARGET_NAME} PROPERTIES MACOSX_BUNDLE_ICON_FILE notepadqq.icns)
endif()

set(NQQ_SANITIZER_FLAGS "")
if(NQQ_ENABLE_ASAN)
    list(APPEND NQQ_SANITIZER_FLAGS -fsanitize=address -fno-omit-frame-pointer)
endif()
if(NQQ_ENABLE_UBSAN)
    list(APPEND NQQ_SANITIZER_FLAGS -fsanitize=undefined -fno-omit-frame-pointer)
endif()
if(NQQ_SANITIZER_FLAGS)
    target_compile_options(${NQQ_TARGET_NAME} PRIVATE ${NQQ_SANITIZER_FLAGS})
    target_link_options(${NQQ_TARGET_NAME} PRIVATE ${NQQ_SANITIZER_FLAGS})
endif()

# ------------------------------------------------------------------ application data
# Language definitions, Notepad++ langs/stylers/themes and the extension tools are staged
# next to the executable ("appdata/"), where Notepadqq::appDataPath() looks for them.
function(nqq_stage_appdata_commands out_var destination)
    set(${out_var}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${NQQ_SUPPORT_DIR}/npp" "${destination}/npp"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${NQQ_SUPPORT_DIR}/languages" "${destination}/languages"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${NQQ_ROOT_DIR}/src/extension_tools" "${destination}/extension_tools"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${destination}/extension_tools/Makefile"
        PARENT_SCOPE)
endfunction()

if(APPLE)
    nqq_stage_appdata_commands(NQQ_BUNDLE_STAGE "$<TARGET_BUNDLE_CONTENT_DIR:${NQQ_TARGET_NAME}>/Resources")
    add_custom_command(TARGET ${NQQ_TARGET_NAME} POST_BUILD ${NQQ_BUNDLE_STAGE}
        COMMENT "Staging application data in app bundle" VERBATIM)
endif()

# Executables are built in the binary directory, so a single "appdata" folder there serves
# the application and the tests (Linux and Windows).
nqq_stage_appdata_commands(NQQ_DIR_STAGE "${NQQ_STAGED_APPDATA_DIR}")
add_custom_target(notepadqq_appdata ALL ${NQQ_DIR_STAGE} COMMENT "Staging application data" VERBATIM)
add_dependencies(${NQQ_TARGET_NAME} notepadqq_appdata)

if(NQQ_BUILD_TESTS)
    enable_testing()

    set(NQQ_UI_TEST_SOURCES ${NQQ_APP_SOURCES})
    list(REMOVE_ITEM NQQ_UI_TEST_SOURCES src/ui/main.cpp)
    qt_add_executable(ui-tests
        src/ui-tests/tst_notepadqqtest.cpp
        ${NQQ_UI_TEST_SOURCES}
        ${NQQ_APP_HEADERS}
        ${NQQ_APP_FORMS}
        ${NQQ_GENERATED_ICONS_QRC}
    )
    if(APPLE)
        set_target_properties(ui-tests PROPERTIES MACOSX_BUNDLE TRUE)
    endif()
    set_target_properties(ui-tests PROPERTIES
        AUTOUIC_SEARCH_PATHS "${NQQ_UI_DIR};${NQQ_UI_DIR}/Search;${NQQ_UI_DIR}/Extensions"
    )
    nqq_configure_target(ui-tests)
    target_link_libraries(ui-tests PRIVATE Qt6::Test)
    if(APPLE)
        nqq_stage_appdata_commands(NQQ_UI_TESTS_STAGE "$<TARGET_BUNDLE_CONTENT_DIR:ui-tests>/Resources")
        add_custom_command(TARGET ui-tests POST_BUILD ${NQQ_UI_TESTS_STAGE} VERBATIM)
    endif()
    add_dependencies(ui-tests notepadqq_appdata)
    add_test(NAME ui-tests COMMAND ui-tests)
    if(NOT WIN32)
        # On Windows the editor views are real Win32 windows, which need the "windows" platform.
        set_tests_properties(ui-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    # Native editor tests: exercise the Scintilla based Editor (search/replace, selections, EOL...).
    qt_add_executable(editor-tests
        src/ui-tests/tst_editor.cpp
        ${NQQ_UI_TEST_SOURCES}
        ${NQQ_APP_HEADERS}
        ${NQQ_APP_FORMS}
        ${NQQ_GENERATED_ICONS_QRC}
    )
    if(APPLE)
        set_target_properties(editor-tests PROPERTIES MACOSX_BUNDLE TRUE)
    endif()
    set_target_properties(editor-tests PROPERTIES
        AUTOUIC_SEARCH_PATHS "${NQQ_UI_DIR};${NQQ_UI_DIR}/Search;${NQQ_UI_DIR}/Extensions"
    )
    nqq_configure_target(editor-tests)
    target_link_libraries(editor-tests PRIVATE Qt6::Test)
    if(APPLE)
        nqq_stage_appdata_commands(NQQ_EDITOR_TESTS_STAGE "$<TARGET_BUNDLE_CONTENT_DIR:editor-tests>/Resources")
        add_custom_command(TARGET editor-tests POST_BUILD ${NQQ_EDITOR_TESTS_STAGE} VERBATIM)
    endif()
    add_dependencies(editor-tests notepadqq_appdata)
    add_test(NAME editor-tests COMMAND editor-tests)
    if(NOT WIN32)
        # On Windows the editor views are real Win32 windows, which need the "windows" platform.
        set_tests_properties(editor-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    if(WIN32 AND NQQ_ENABLE_NPP_PLUGINS)
        # A real Notepad++ plugin DLL, loaded by the plugin host from <build dir>/plugins.
        add_library(NqqTestPlugin SHARED src/ui-tests/npp-test-plugin/NqqTestPlugin.cpp)
        target_include_directories(NqqTestPlugin PRIVATE src/ui/NppPlugins/npp third_party/scintilla/include)
        target_compile_definitions(NqqTestPlugin PRIVATE UNICODE _UNICODE NOMINMAX)
        target_link_libraries(NqqTestPlugin PRIVATE user32)
        set_target_properties(NqqTestPlugin PROPERTIES
            AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
            PREFIX ""
            OUTPUT_NAME NqqTestPlugin
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/NqqTestPlugin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/NqqTestPlugin"
        )

        qt_add_executable(npp-plugin-tests
            src/ui-tests/tst_npppluginhost.cpp
            ${NQQ_UI_TEST_SOURCES}
            ${NQQ_APP_HEADERS}
            ${NQQ_APP_FORMS}
            ${NQQ_GENERATED_ICONS_QRC}
        )
        set_target_properties(npp-plugin-tests PROPERTIES
            AUTOUIC_SEARCH_PATHS "${NQQ_UI_DIR};${NQQ_UI_DIR}/Search;${NQQ_UI_DIR}/Extensions"
        )
        nqq_configure_target(npp-plugin-tests)
        target_link_libraries(npp-plugin-tests PRIVATE Qt6::Test)
        add_dependencies(npp-plugin-tests notepadqq_appdata NqqTestPlugin)
        add_test(NAME npp-plugin-tests COMMAND npp-plugin-tests)
    endif()

    qt_add_executable(cpp-correctness-tests
        src/ui-tests/tst_cppcorrectness.cpp
        src/ui/EditorNS/asyncrequesttracker.cpp
        src/ui/include/EditorNS/asyncrequesttracker.h
        src/ui/Search/filesearcher.cpp
        src/ui/include/Search/filesearcher.h
        src/ui/Search/searchhelpers.cpp
        src/ui/Search/searchobjects.cpp
        src/ui/Search/searchstring.cpp
        src/ui/statsruntime.cpp
        src/ui/localsockethelpers.cpp
        src/ui/include/localsockethelpers.h
        src/ui/commandlineopenruntime.cpp
        src/ui/include/commandlineopenruntime.h
        src/ui/Extensions/extension.cpp
        src/ui/include/Extensions/extension.h
        src/ui/encodingdetector.cpp
        src/ui/include/encodingdetector.h
    )
    nqq_configure_target(cpp-correctness-tests)
    target_link_libraries(cpp-correctness-tests PRIVATE Qt6::Test)
    target_compile_definitions(cpp-correctness-tests PRIVATE NQQ_CPP_CORRECTNESS_TESTS)
    add_test(NAME cpp-correctness-tests COMMAND cpp-correctness-tests)
    if(NOT WIN32)
        # On Windows the editor views are real Win32 windows, which need the "windows" platform.
        set_tests_properties(cpp-correctness-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

if(APPLE)
    install(TARGETS ${NQQ_TARGET_NAME}
        BUNDLE DESTINATION .
    )
elseif(WIN32)
    # Portable layout: notepadqq.exe, appdata\ and plugins\ side by side.
    install(TARGETS ${NQQ_TARGET_NAME} RUNTIME DESTINATION .)
    install(DIRECTORY "${NQQ_SUPPORT_DIR}/npp" "${NQQ_SUPPORT_DIR}/languages" DESTINATION appdata)
    install(DIRECTORY "${NQQ_ROOT_DIR}/src/extension_tools/" DESTINATION appdata/extension_tools
        PATTERN "Makefile" EXCLUDE)
    install(FILES "${NQQ_SUPPORT_DIR}/windows/plugins-README.txt" DESTINATION plugins RENAME README.txt)
else()
    install(TARGETS ${NQQ_TARGET_NAME}
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()

if(UNIX AND NOT APPLE)
    install(DIRECTORY "${NQQ_STAGED_APPDATA_DIR}/npp" "${NQQ_STAGED_APPDATA_DIR}/languages"
        "${NQQ_STAGED_APPDATA_DIR}/extension_tools"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/notepadqq"
    )
    install(FILES "${NQQ_SUPPORT_DIR}/shortcuts/notepadqq.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
    install(FILES "${NQQ_SUPPORT_DIR}/com.notepadqq.Notepadqq.metainfo.xml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
    )
    install(FILES "${NQQ_SUPPORT_DIR}/manpage/notepadqq.1"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
    )

    foreach(NQQ_ICON_SIZE 16 22 24 32 48 64 96 128 256 512)
        install(FILES "${NQQ_SUPPORT_DIR}/icons/hicolor/${NQQ_ICON_SIZE}x${NQQ_ICON_SIZE}/apps/notepadqq.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${NQQ_ICON_SIZE}x${NQQ_ICON_SIZE}/apps"
        )
    endforeach()
    install(FILES "${NQQ_SUPPORT_DIR}/icons/hicolor/scalable/apps/notepadqq.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
    )
endif()

if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        @ONLY
    )

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
        COMMENT "Uninstalling Notepadqq"
        VERBATIM
    )
endif()
