; Notepadqq Windows installer (NSIS 3, Unicode, x64).
;
; Build (done by .github/workflows/windows.yml):
;   makensis /DVERSION=2.2.0 /DVERSION_NUMERIC=2.2.0.0 /DDISTDIR=<deployed app folder> /DOUTFILE=<setup.exe> installer.nsi
;
; Installs the deployed application folder (notepadqq.exe, Qt runtime, appdata\, plugins\) into
; "Program Files\Notepadqq", with Start menu / desktop shortcuts, an optional "Edit with
; Notepadqq" Explorer context menu entry, "Open with" registration and an uninstaller listed in
; "Apps & features". User settings and sessions (in %APPDATA%\Notepadqq) are left untouched.

Unicode true
SetCompressor /SOLID lzma
ManifestDPIAware true

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef VERSION_NUMERIC
  !define VERSION_NUMERIC "0.0.0.0"
!endif
!ifndef DISTDIR
  !error "DISTDIR must point to the deployed application folder"
!endif
!ifndef OUTFILE
  !define OUTFILE "Notepadqq-${VERSION}-x64-setup.exe"
!endif

!define APPNAME "Notepadqq"
!define PUBLISHER "Notepadqq"
!define EXE "notepadqq.exe"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
!define APPPATHKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\${EXE}"

!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "${UNINSTKEY}" "InstallLocation"
RequestExecutionLevel admin
BrandingText "${APPNAME} ${VERSION}"

VIProductVersion "${VERSION_NUMERIC}"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "GNU General Public License v3"

!define MUI_ICON "notepadqq.ico"
!define MUI_UNICON "notepadqq.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Run ${APPNAME}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\COPYING"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${APPNAME} requires a 64-bit version of Windows."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "!${APPNAME} (required)" SecApp
  SectionIn RO
  SetShellVarContext all
  SetOutPath "$INSTDIR"
  File /r "${DISTDIR}\*.*"

  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Start menu
  CreateShortcut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0

  ; "Apps & features" entry
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\${EXE},0"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegStr HKLM "${UNINSTKEY}" "URLInfoAbout" "https://notepadqq.com"
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINSTKEY}" "EstimatedSize" "$0"

  ; "notepadqq" from Win+R / the command line
  WriteRegStr HKLM "${APPPATHKEY}" "" "$INSTDIR\${EXE}"
  WriteRegStr HKLM "${APPPATHKEY}" "Path" "$INSTDIR"

  ; "Open with" list
  WriteRegStr HKLM "Software\Classes\Applications\${EXE}" "FriendlyAppName" "${APPNAME}"
  WriteRegStr HKLM "Software\Classes\Applications\${EXE}\shell\open\command" "" '"$INSTDIR\${EXE}" "%1"'
  WriteRegStr HKLM "Software\Classes\Applications\${EXE}\SupportedTypes" ".txt" ""
  WriteRegStr HKLM "Software\Classes\Applications\${EXE}\SupportedTypes" ".log" ""
  WriteRegStr HKLM "Software\Classes\Applications\${EXE}\SupportedTypes" ".ini" ""
  WriteRegStr HKLM "Software\Classes\Applications\${EXE}\SupportedTypes" ".xml" ""
  WriteRegStr HKLM "Software\Classes\Applications\${EXE}\SupportedTypes" ".json" ""
  WriteRegStr HKLM "Software\Classes\.txt\OpenWithList\${EXE}" "" ""
SectionEnd

Section "Desktop shortcut" SecDesktop
  SetShellVarContext all
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
SectionEnd

Section "$\"Edit with ${APPNAME}$\" in the Explorer context menu" SecContextMenu
  WriteRegStr HKLM "Software\Classes\*\shell\${APPNAME}" "" "Edit with ${APPNAME}"
  WriteRegStr HKLM "Software\Classes\*\shell\${APPNAME}" "Icon" "$INSTDIR\${EXE},0"
  WriteRegStr HKLM "Software\Classes\*\shell\${APPNAME}\command" "" '"$INSTDIR\${EXE}" "%1"'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "The ${APPNAME} editor, Notepad++ languages and themes, and the plugins folder."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create a shortcut on the desktop."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecContextMenu} "Add $\"Edit with ${APPNAME}$\" when right-clicking a file in Explorer."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  SetShellVarContext all
  Delete "$SMPROGRAMS\${APPNAME}.lnk"
  Delete "$DESKTOP\${APPNAME}.lnk"

  DeleteRegKey HKLM "Software\Classes\*\shell\${APPNAME}"
  DeleteRegKey HKLM "Software\Classes\Applications\${EXE}"
  DeleteRegKey HKLM "Software\Classes\.txt\OpenWithList\${EXE}"
  DeleteRegKey HKLM "${APPPATHKEY}"
  DeleteRegKey HKLM "${UNINSTKEY}"

  ; Remove the installed files, but keep plugins the user added to the plugins folder.
  RMDir /r "$INSTDIR\appdata"
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\iconengines"
  RMDir /r "$INSTDIR\generic"
  RMDir /r "$INSTDIR\networkinformation"
  RMDir /r "$INSTDIR\tls"
  RMDir /r "$INSTDIR\printsupport"
  RMDir /r "$INSTDIR\translations"
  Delete "$INSTDIR\*.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\plugins\README.txt"
  RMDir "$INSTDIR\plugins"
  RMDir "$INSTDIR"
SectionEnd
