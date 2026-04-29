; ===================================================================
; Group 13 - VR CAD Viewer  (EEEE2076)
; NSIS installer script
; ===================================================================

!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

; --- Product metadata ---------------------------------------------------
!define PRODUCT_NAME        "Group 13 VR CAD Viewer"
!define PRODUCT_VERSION     "1.0.0"
!define PRODUCT_PUBLISHER   "EEEE2076 Group 13 - University of Nottingham"
!define PRODUCT_EXE         "ws6.exe"
!define PRODUCT_LAUNCHER    "Run.bat"
!define PRODUCT_REGKEY      "Software\Group13\VRViewer"
!define PRODUCT_UNINST_KEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\Group13VRViewer"

; --- General ------------------------------------------------------------
Name        "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile     "D:\Group13_VRViewer_Setup.exe"
InstallDir  "$PROGRAMFILES64\Group13 VR Viewer"
InstallDirRegKey HKLM "${PRODUCT_REGKEY}" "InstallDir"
RequestExecutionLevel admin
Unicode true

; --- Compression (1.1 GB payload -> use solid LZMA) ---------------------
SetCompressor /SOLID lzma
SetCompressorDictSize 64

; --- Modern UI ----------------------------------------------------------
!define MUI_ABORTWARNING
!define MUI_ICON   "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN          "$INSTDIR\${PRODUCT_LAUNCHER}"
!define MUI_FINISHPAGE_RUN_TEXT     "Launch ${PRODUCT_NAME} now (requires SteamVR for VR mode)"
!define MUI_FINISHPAGE_SHOWREADME   "$INSTDIR\README.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Open README"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; Languages
!insertmacro MUI_LANGUAGE "English"

; Version Info (shows up in Explorer properties)
VIProductVersion "1.0.0.0"
VIAddVersionKey  "ProductName"     "${PRODUCT_NAME}"
VIAddVersionKey  "CompanyName"     "${PRODUCT_PUBLISHER}"
VIAddVersionKey  "FileDescription" "${PRODUCT_NAME} Setup"
VIAddVersionKey  "FileVersion"     "${PRODUCT_VERSION}"
VIAddVersionKey  "ProductVersion"  "${PRODUCT_VERSION}"
VIAddVersionKey  "LegalCopyright"  "(c) 2026 ${PRODUCT_PUBLISHER}"

; --- Init: enforce 64-bit ----------------------------------------------
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "This application is 64-bit only and cannot be installed on a 32-bit version of Windows."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

; --- Install section ----------------------------------------------------
Section "Application (required)" SecMain
  SectionIn RO

  SetOutPath "$INSTDIR"
  SetOverwrite on

  ; Bundle the entire prepared package_debug folder
  File /r "package_debug\*.*"

  ; Registry: install location + uninstall info
  WriteRegStr HKLM "${PRODUCT_REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${PRODUCT_REGKEY}" "Version"    "${PRODUCT_VERSION}"

  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayName"     "${PRODUCT_NAME}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion"  "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "Publisher"       "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${PRODUCT_EXE}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoRepair" 1

  ; Record install size for Add/Remove Programs
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "EstimatedSize" "$0"

  ; Write uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Start Menu Shortcuts" SecStartMenu
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" \
                  "$INSTDIR\${PRODUCT_LAUNCHER}" "" "$INSTDIR\${PRODUCT_EXE}" 0 \
                  SW_SHOWMINIMIZED "" "Launch ${PRODUCT_NAME} (checks SteamVR)"
  CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME} (no SteamVR check).lnk" \
                  "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\${PRODUCT_EXE}" 0 \
                  SW_SHOWNORMAL "" "Launch ws6.exe directly"
  CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\README.lnk" \
                  "$INSTDIR\README.txt"
  CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall ${PRODUCT_NAME}.lnk" \
                  "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop Shortcut" SecDesktop
  CreateShortcut  "$DESKTOP\${PRODUCT_NAME}.lnk" \
                  "$INSTDIR\${PRODUCT_LAUNCHER}" "" "$INSTDIR\${PRODUCT_EXE}" 0 \
                  SW_SHOWMINIMIZED "" "Launch ${PRODUCT_NAME}"
SectionEnd

; --- Component descriptions --------------------------------------------
LangString DESC_SecMain      ${LANG_ENGLISH} "The application binaries, Qt + VTK + OpenVR runtime DLLs, MSVC redistributable, controller bindings, and bundled Models."
LangString DESC_SecStartMenu ${LANG_ENGLISH} "Add Start Menu entries for launching and uninstalling the application."
LangString DESC_SecDesktop   ${LANG_ENGLISH} "Place a shortcut on the Desktop."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain}      $(DESC_SecMain)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} $(DESC_SecStartMenu)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop}   $(DESC_SecDesktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; --- Uninstall section --------------------------------------------------
Section "Uninstall"
  ; Shortcuts
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME} (no SteamVR check).lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\README.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall ${PRODUCT_NAME}.lnk"
  RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"

  ; Application files (everything we installed lives under $INSTDIR)
  RMDir /r "$INSTDIR"

  ; Registry
  DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_REGKEY}"
  DeleteRegKey /ifempty HKLM "Software\Group13"
SectionEnd
