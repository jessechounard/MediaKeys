!include "MUI2.nsh"

!searchparse /file "src\version.h" `#define VERSION "` VERSION `"`

Name "MediaKeys"
OutFile "MediaKeys-${VERSION}-Setup.exe"
InstallDir "$PROGRAMFILES\MediaKeys"
RequestExecutionLevel admin

!define MUI_ICON "assets\icon.ico"
!define MUI_UNICON "assets\icon.ico"

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\MediaKeys.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Run MediaKeys"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Run at startup"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION EnableRunAtStartup
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function EnableRunAtStartup
    CreateShortcut "$SMSTARTUP\MediaKeys.lnk" "$INSTDIR\MediaKeys.exe" "" "$INSTDIR\icon.ico"
FunctionEnd

Section "Install"
    SetOutPath $INSTDIR
    File "zig-out\bin\MediaKeys.exe"
    File "assets\icon.ico"

    WriteUninstaller "$INSTDIR\Uninstall.exe"

    CreateDirectory "$SMPROGRAMS\MediaKeys"
    CreateShortcut "$SMPROGRAMS\MediaKeys\MediaKeys.lnk" "$INSTDIR\MediaKeys.exe" "" "$INSTDIR\icon.ico"
    CreateShortcut "$SMPROGRAMS\MediaKeys\Uninstall.lnk" "$INSTDIR\Uninstall.exe"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MediaKeys" "DisplayName" "MediaKeys"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MediaKeys" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MediaKeys" "Publisher" "Jesse Chounard"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MediaKeys" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MediaKeys" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MediaKeys" "DisplayIcon" "$INSTDIR\icon.ico"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MediaKeys" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MediaKeys" "NoRepair" 1
SectionEnd

Section "Uninstall"
    nsExec::ExecToLog 'taskkill /F /IM MediaKeys.exe'

    Delete "$INSTDIR\MediaKeys.exe"
    Delete "$INSTDIR\icon.ico"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    Delete "$SMPROGRAMS\MediaKeys\MediaKeys.lnk"
    Delete "$SMPROGRAMS\MediaKeys\Uninstall.lnk"
    RMDir "$SMPROGRAMS\MediaKeys"

    Delete "$SMSTARTUP\MediaKeys.lnk"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MediaKeys"

    MessageBox MB_YESNO "Do you want to delete your configuration and log files?" IDNO SkipConfigDelete
        RMDir /r "$APPDATA\MediaKeys"
    SkipConfigDelete:
SectionEnd
