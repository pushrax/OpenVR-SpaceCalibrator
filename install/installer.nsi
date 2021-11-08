;--------------------------------
;Include Modern UI

	!include "MUI2.nsh"

;--------------------------------
;General

	!define OVERLAY_BASEDIR "..\client_overlay\bin\win64"
	!define DRIVER_RESDIR "..\OpenVR-SpaceCalibratorDriver\01spacecalibrator"

	Name "OpenVR-SpaceCalibrator"
	OutFile "OpenVR-SpaceCalibrator.exe"
	InstallDir "$PROGRAMFILES64\OpenVR-SpaceCalibrator"
	InstallDirRegKey HKLM "Software\OpenVR-SpaceCalibrator\Main" ""
	RequestExecutionLevel admin
	ShowInstDetails show
	
;--------------------------------
;Variables

VAR upgradeInstallation

;--------------------------------
;Interface Settings

	!define MUI_ABORTWARNING

;--------------------------------
;Pages

	!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
	!define MUI_PAGE_CUSTOMFUNCTION_PRE dirPre
	!insertmacro MUI_PAGE_DIRECTORY
	!insertmacro MUI_PAGE_INSTFILES
  
	!insertmacro MUI_UNPAGE_CONFIRM
	!insertmacro MUI_UNPAGE_INSTFILES
  
;--------------------------------
;Languages
 
	!insertmacro MUI_LANGUAGE "English"

;--------------------------------
;Macros

;--------------------------------
;Functions

Function dirPre
	StrCmp $upgradeInstallation "true" 0 +2 
		Abort
FunctionEnd

Function .onInit
	StrCpy $upgradeInstallation "false"
 
	ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVRSpaceCalibrator" "UninstallString"
	StrCmp $R0 "" done
	
	
	; If SteamVR is already running, display a warning message and exit
	FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
	StrCmp $0 0 +3
		MessageBox MB_OK|MB_ICONEXCLAMATION \
			"SteamVR is still running. Cannot install this software.$\nPlease close SteamVR and try again."
		Abort
 
	
	MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
		"OpenVR-SpaceCalibrator is already installed. $\n$\nClick `OK` to upgrade the \
		existing installation or `Cancel` to cancel this upgrade." \
		IDOK upgrade
	Abort
 
	upgrade:
		StrCpy $upgradeInstallation "true"
	done:
FunctionEnd

;--------------------------------
;Installer Sections

Section "Install" SecInstall
	
	StrCmp $upgradeInstallation "true" 0 noupgrade 
		DetailPrint "Uninstall previous version..."
		ExecWait '"$INSTDIR\Uninstall.exe" /S _?=$INSTDIR'
		Delete $INSTDIR\Uninstall.exe
		Goto afterupgrade
		
	noupgrade:

	afterupgrade:

	SetOutPath "$INSTDIR"

	File "..\LICENSE"
	File "..\x64\Release\OpenVR-SpaceCalibrator.exe"
	File "..\lib\openvr\lib\win64\openvr_api.dll"
	File "..\OpenVR-SpaceCalibrator\manifest.vrmanifest"
	File "..\OpenVR-SpaceCalibrator\icon.png"

	ExecWait '"$INSTDIR\vcredist_x64.exe" /install /quiet'
	
	Var /GLOBAL vrRuntimePath
	nsExec::ExecToStack '"$INSTDIR\OpenVR-SpaceCalibrator.exe" -openvrpath'
	Pop $0
	Pop $vrRuntimePath
	DetailPrint "VR runtime path: $vrRuntimePath"
	
	; Old beta driver
	StrCmp $upgradeInstallation "true" 0 nocleanupbeta 
		Delete "$vrRuntimePath\drivers\000spacecalibrator\driver.vrdrivermanifest"
		Delete "$vrRuntimePath\drivers\000spacecalibrator\resources\driver.vrresources"
		Delete "$vrRuntimePath\drivers\000spacecalibrator\resources\settings\default.vrsettings"
		Delete "$vrRuntimePath\drivers\000spacecalibrator\bin\win64\driver_000spacecalibrator.dll"
		Delete "$vrRuntimePath\drivers\000spacecalibrator\bin\win64\space_calibrator_driver.log"
		RMdir "$vrRuntimePath\drivers\000spacecalibrator\resources\settings"
		RMdir "$vrRuntimePath\drivers\000spacecalibrator\resources\"
		RMdir "$vrRuntimePath\drivers\000spacecalibrator\bin\win64\"
		RMdir "$vrRuntimePath\drivers\000spacecalibrator\bin\"
		RMdir "$vrRuntimePath\drivers\000spacecalibrator\"
	nocleanupbeta:

	SetOutPath "$vrRuntimePath\drivers\01spacecalibrator"
	File "${DRIVER_RESDIR}\driver.vrdrivermanifest"
	SetOutPath "$vrRuntimePath\drivers\01spacecalibrator\resources"
	File "${DRIVER_RESDIR}\resources\driver.vrresources"
	SetOutPath "$vrRuntimePath\drivers\01spacecalibrator\resources\settings"
	File "${DRIVER_RESDIR}\resources\settings\default.vrsettings"
	SetOutPath "$vrRuntimePath\drivers\01spacecalibrator\bin\win64"
	File "..\x64\Release\driver_01spacecalibrator.dll"
	
	WriteRegStr HKLM "Software\OpenVR-SpaceCalibrator\Main" "" $INSTDIR
	WriteRegStr HKLM "Software\OpenVR-SpaceCalibrator\Driver" "" $vrRuntimePath
  
	WriteUninstaller "$INSTDIR\Uninstall.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVRSpaceCalibrator" "DisplayName" "OpenVR-SpaceCalibrator"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVRSpaceCalibrator" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""

	CreateShortCut "$SMPROGRAMS\OpenVR-SpaceCalibrator.lnk" "$INSTDIR\OpenVR-SpaceCalibrator.exe"
	
	SetOutPath "$INSTDIR"
	nsExec::ExecToLog '"$INSTDIR\OpenVR-SpaceCalibrator.exe" -installmanifest'
	nsExec::ExecToLog '"$INSTDIR\OpenVR-SpaceCalibrator.exe" -activatemultipledrivers'

SectionEnd

;--------------------------------
;Uninstaller Section

Section "Uninstall"
	; If SteamVR is already running, display a warning message and exit
	FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
	StrCmp $0 0 +3
		MessageBox MB_OK|MB_ICONEXCLAMATION \
			"SteamVR is still running. Cannot uninstall this software.$\nPlease close SteamVR and try again."
		Abort
	
	SetOutPath "$INSTDIR"
	nsExec::ExecToLog '"$INSTDIR\OpenVR-SpaceCalibrator.exe" -removemanifest'

	Var /GLOBAL vrRuntimePath2
	ReadRegStr $vrRuntimePath2 HKLM "Software\OpenVR-SpaceCalibrator\Driver" ""
	DetailPrint "VR runtime path: $vrRuntimePath2"
	Delete "$vrRuntimePath2\drivers\01spacecalibrator\driver.vrdrivermanifest"
	Delete "$vrRuntimePath2\drivers\01spacecalibrator\resources\driver.vrresources"
	Delete "$vrRuntimePath2\drivers\01spacecalibrator\resources\settings\default.vrsettings"
	Delete "$vrRuntimePath2\drivers\01spacecalibrator\bin\win64\driver_01spacecalibrator.dll"
	Delete "$vrRuntimePath2\drivers\01spacecalibrator\bin\win64\space_calibrator_driver.log"
	RMdir "$vrRuntimePath2\drivers\01spacecalibrator\resources\settings"
	RMdir "$vrRuntimePath2\drivers\01spacecalibrator\resources\"
	RMdir "$vrRuntimePath2\drivers\01spacecalibrator\bin\win64\"
	RMdir "$vrRuntimePath2\drivers\01spacecalibrator\bin\"
	RMdir "$vrRuntimePath2\drivers\01spacecalibrator\"

	Delete "$INSTDIR\LICENSE"
	Delete "$INSTDIR\OpenVR-SpaceCalibrator.exe"
	Delete "$INSTDIR\openvr_api.dll"
	Delete "$INSTDIR\manifest.vrmanifest"
	Delete "$INSTDIR\icon.png"
	
	DeleteRegKey HKLM "Software\OpenVR-SpaceCalibrator\Main"
	DeleteRegKey HKLM "Software\OpenVR-SpaceCalibrator\Driver"
	DeleteRegKey HKLM "Software\OpenVR-SpaceCalibrator"
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVRSpaceCalibrator"

	Delete "$SMPROGRAMS\OpenVR-SpaceCalibrator.lnk"
SectionEnd

