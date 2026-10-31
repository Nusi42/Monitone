; Monitone setup (NSIS 3). Built by tools/build-release.ps1:
;
;   makensis /DVERSION=X.Y.Z /DBINDIR=<folder with the binaries> /DOUTFILE=<setup.exe> installer\Monitone.nsi
;
; Per-user installation without administrator rights, like the MSI package.

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WinMessages.nsh"

!ifndef VERSION
  !error "Define VERSION."
!endif
!ifndef BINDIR
  !error "Define BINDIR."
!endif
!ifndef OUTFILE
  !error "Define OUTFILE."
!endif

!define ROOT "${__FILEDIR__}\.."
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Monitone"
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"
!define TRAY_WINDOW_CLASS "Monitone.Tray"

Name "Monitone"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\Monitone"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
BrandingText "Monitone ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Monitone"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "Monitone Setup"
VIAddVersionKey "CompanyName" "Monitone contributors"
VIAddVersionKey "LegalCopyright" "AGPL-3.0-or-later"

!define MUI_ICON "${ROOT}\res\Monitone.ico"
!define MUI_UNICON "${ROOT}\res\Monitone.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "Monitone plays every program on the speakers of the screen it is on.$\r$\n$\r$\nNo administrator rights are needed. If Monitone is running, setup closes it first."
!define MUI_FINISHPAGE_RUN "$INSTDIR\Monitone.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start Monitone"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; Asks a running Monitone to exit (it restores the Windows audio settings on
; exit) and waits up to five seconds for it.
!macro CloseMonitone
  FindWindow $0 "${TRAY_WINDOW_CLASS}"
  ${If} $0 <> 0
    DetailPrint "Closing Monitone..."
    SendMessage $0 ${WM_CLOSE} 0 0
    StrCpy $1 0
    ${Do}
      Sleep 200
      IntOp $1 $1 + 1
      FindWindow $0 "${TRAY_WINDOW_CLASS}"
      ${If} $0 = 0
      ${OrIf} $1 >= 25
        ${ExitDo}
      ${EndIf}
    ${Loop}
    ; The process may still be releasing its files.
    Sleep 500
  ${EndIf}
!macroend

Section "Monitone"
  !insertmacro CloseMonitone

  SetOutPath "$INSTDIR"
  File "${BINDIR}\Monitone.exe"
  File "${BINDIR}\MonitoneSettings.dll"
  File "/oname=LICENSE.txt" "${ROOT}\LICENSE"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateShortcut "$SMPROGRAMS\Monitone.lnk" "$INSTDIR\Monitone.exe"

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "Monitone"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Monitone contributors"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\Monitone.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/Nusi42/Monitone"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" $0
SectionEnd

Section "Uninstall"
  !insertmacro CloseMonitone

  ; Start with Windows and the WebView2 cache; the settings in
  ; %APPDATA%\Monitone are kept for a later installation.
  DeleteRegValue HKCU "${RUN_KEY}" "Monitone"
  RMDir /r "$LOCALAPPDATA\Monitone"

  Delete "$SMPROGRAMS\Monitone.lnk"
  Delete "$INSTDIR\Monitone.exe"
  Delete "$INSTDIR\MonitoneSettings.dll"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKCU "${UNINSTALL_KEY}"
SectionEnd
