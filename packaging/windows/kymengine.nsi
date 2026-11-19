!include "MUI2.nsh"

!ifndef VERSION
  !define VERSION "0.1.0"
!endif
!ifndef DIST_DIR
  !define DIST_DIR "..\..\build\dist\KymEngine-windows-x64"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "..\..\build\dist\KymEngine-${VERSION}-win64-setup.exe"
!endif

Name "KymEngine ${VERSION}"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\KymEngine"
InstallDirRegKey HKLM "Software\KymEngine" "Install_Dir"
RequestExecutionLevel admin
Unicode True

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "English"

Section "KymEngine (Editor + Runtime)" SecMain
  SetOutPath "$INSTDIR"
  File /r "${DIST_DIR}\*.*"

  WriteRegStr HKLM "Software\KymEngine" "Install_Dir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KymEngine" "DisplayName" "KymEngine ${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KymEngine" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KymEngine" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KymEngine" "NoRepair" 1
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\KymEngine"
  CreateShortcut "$SMPROGRAMS\KymEngine\KymEngine Hub.lnk" "$INSTDIR\KymEngine.bat" "" "$INSTDIR\KymEngineHub.exe" 0
  CreateShortcut "$SMPROGRAMS\KymEngine\Desinstalar KymEngine.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\KymEngine Hub.lnk" "$INSTDIR\KymEngine.bat" "" "$INSTDIR\KymEngineHub.exe" 0
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\KymEngine.lnk"
  Delete "$DESKTOP\KymEngine Hub.lnk"
  RMDir /r "$SMPROGRAMS\KymEngine"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KymEngine"
  DeleteRegKey HKLM "Software\KymEngine"
SectionEnd
