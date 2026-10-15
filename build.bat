@echo off
rem Compile la layer (C++) et le programme de capteurs (C#), puis rassemble le tout dans dist\.
rem Tout ce qui s'affiche est aussi enregistre dans build.log, et la fenetre reste ouverte a la fin.
rem Pour compiler ET installer en une fois, lancez plutot Installer.bat.
rem   build.bat --auto : sans pause finale (utilise par Installer.bat).
rem   Variable XRPO_DIST : dossier de sortie a la place de dist (chemin relatif a ce dossier).
setlocal
cd /d "%~dp0"
set "OUT=dist"
if defined XRPO_DIST set "OUT=%XRPO_DIST%"
if "%~1"=="--inner" goto :main
set "AUTO="
if /i "%~1"=="--auto" set "AUTO=1"

echo Compilation en cours, cela peut prendre une a deux minutes...
call "%~f0" --inner > "%~dp0build.log" 2>&1
set "RC=%errorlevel%"
type "%~dp0build.log"
echo.
echo ------------------------------------------------------------
if "%RC%"=="0" goto :ok
if "%RC%"=="20" goto :nosdk
if "%RC%"=="21" goto :csfail
echo ECHEC de la compilation de la layer, code %RC%. Le detail est ci-dessus et dans build.log.
goto :end
:nosdk
echo La layer est compilee, mais le programme de capteurs n'a PAS ete compile :
echo aucun SDK .NET 8 ou plus recent n'a ete trouve.
echo Installez-le avec :  winget install Microsoft.DotNet.SDK.10
echo ou, dans Visual Studio Installer, ajoutez la charge "Developpement .NET Desktop", puis recommencez.
goto :end
:csfail
echo La layer est compilee, mais le programme de capteurs n'a PAS pu etre compile.
echo Le detail est ci-dessus et dans build.log.
goto :end
:ok
if defined AUTO goto :end
echo Termine : le resultat est dans le dossier %OUT%\.
echo Pour l'installer, le plus simple est de lancer Installer.bat.
:end
echo ------------------------------------------------------------
if not defined AUTO pause
exit /b %RC%


:main
echo === Environnement ===
rem Si cmake n'est pas dans le PATH, on charge l'environnement de Visual Studio.
where cmake >nul 2>nul
if not errorlevel 1 goto :tools
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :novs
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto :novs
if not exist "%VSDIR%\Common7\Tools\VsDevCmd.bat" goto :novs
rem VSCMD_START_DIR empeche VsDevCmd de changer de dossier courant.
set "VSCMD_START_DIR=%CD%"
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
cd /d "%~dp0"

:tools
where cmake >nul 2>nul
if errorlevel 1 goto :nocmake
where cmake
cmake --version

echo.
echo === Layer OpenXR (C++) ===
cmake -S layer -B build\layer -A x64
if errorlevel 1 exit /b 10
cmake --build build\layer --config Release
if errorlevel 1 exit /b 11

echo.
echo === Assemblage de %OUT%\ ===
if not exist "%OUT%" mkdir "%OUT%"
copy /y build\layer\Release\XR_APILAYER_NOVENDOR_perf_overlay.dll "%OUT%\"
if errorlevel 1 exit /b 12
copy /y layer\XR_APILAYER_NOVENDOR_perf_overlay.json "%OUT%\"
if errorlevel 1 exit /b 12
if not exist "%OUT%\XRPerfOverlay.ini" copy /y layer\XRPerfOverlay.ini "%OUT%\"
copy /y scripts\install.ps1 "%OUT%\"
copy /y scripts\uninstall.ps1 "%OUT%\"
copy /y scripts\install.bat "%OUT%\"
copy /y scripts\uninstall.bat "%OUT%\"
if exist VERSION copy /y VERSION "%OUT%\"

echo.
echo === Programme de capteurs (C#) ===
where dotnet >nul 2>nul
if errorlevel 1 goto :nosdkinner
where dotnet
echo SDK .NET installes :
dotnet --list-sdks
rem Il faut un SDK 8 ou plus recent (le simple "runtime" .NET ne suffit pas pour compiler).
set "SDKOK="
for /f "tokens=1 delims=." %%v in ('dotnet --list-sdks 2^>nul') do if %%v GEQ 8 set "SDKOK=1"
if not defined SDKOK goto :nosdkinner
dotnet publish sensors\XRPerfSensors.csproj -c Release -o "%OUT%\sensors" --nologo
if errorlevel 1 exit /b 21
rem XRPerfVR (jeux SteamVR / OpenVR) : a cote de XRPerfSensors, qui le lance.
copy /y build\layer\Release\XRPerfVR.exe "%OUT%\sensors\"
if errorlevel 1 exit /b 13
copy /y build\layer\Release\openvr_api.dll "%OUT%\sensors\"
if errorlevel 1 exit /b 13
rem XRPerfCapture.dll (fenetre du bureau affichee dans le casque) : chargee par XRPerfSensors.
copy /y build\layer\Release\XRPerfCapture.dll "%OUT%\sensors\"
if errorlevel 1 exit /b 13
echo.
echo Compilation terminee.
exit /b 0

:nosdkinner
echo Aucun SDK .NET 8 ou plus recent trouve.
exit /b 20
:novs
echo Visual Studio avec la charge "Developpement Desktop en C++" est introuvable.
exit /b 2
:nocmake
echo CMake est introuvable. Dans Visual Studio Installer, ajoutez "Outils CMake C++ pour Windows".
exit /b 3
