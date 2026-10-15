@echo off
rem XR Perf Overlay - installation ou mise a jour en un double-clic.
rem Deux cas :
rem   - paquet de distribution (sous-dossier "programme" deja compile) : installation directe ;
rem   - sources : compile d'abord la layer et le programme de capteurs (meme chose que build.bat).
rem Puis copie le programme dans le dossier d'installation en gardant vos reglages,
rem enregistre la layer aupres d'OpenXR et relance XRPerfSensors.
rem Options facultatives :  Installer.bat -Target "D:\Dossier"   -AutoStart oui   -AutoStart non
setlocal
cd /d "%~dp0"
echo ============================================================
echo  XR Perf Overlay - installation
echo ============================================================
echo.
rem Paquet de distribution : le programme est deja compile, rien a compiler.
if exist "%~dp0programme\XR_APILAYER_NOVENDOR_perf_overlay.dll" goto :prebuilt
if not exist "%~dp0build.bat" goto :incomplete
if not exist "%~dp0layer\CMakeLists.txt" goto :incomplete
if not exist "%~dp0sensors\XRPerfSensors.csproj" goto :incomplete
if not exist "%~dp0scripts\setup.ps1" goto :incomplete

rem La compilation se fait dans un dossier a part (build\package), jamais dans un dossier installe :
rem une version en cours d'utilisation ne gene donc pas, et rien d'ancien ne se melange au nouveau.
set "XRPO_DIST=build\package"
if exist "%XRPO_DIST%" rmdir /s /q "%XRPO_DIST%"

echo Etape 1 sur 2 : compilation
call "%~dp0build.bat" --auto
if errorlevel 1 goto :buildfailed

echo.
echo Etape 2 sur 2 : installation
goto :install

:prebuilt
if not exist "%~dp0scripts\setup.ps1" goto :incomplete
set "XRPO_DIST=programme"

:install
echo Windows va demander une autorisation d'administrateur : repondez Oui.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup.ps1" -Source "%~dp0%XRPO_DIST%" %*
set "RC=%errorlevel%"
rem 0 = termine (ou poursuivi dans la fenetre d'administrateur) ; 3 = arret deja explique par le script.
if "%RC%"=="0" exit /b 0
if "%RC%"=="3" exit /b 1
echo.
echo PowerShell n'a pas pu executer le script d'installation, code %RC%.
echo Le message d'erreur est juste au-dessus.
pause
exit /b 1

:buildfailed
echo.
echo La compilation n'a pas abouti : rien n'a ete installe ni modifie.
echo Le detail est ci-dessus et dans le fichier build.log, a cote de Installer.bat.
pause
exit /b 1

:incomplete
echo Il manque des fichiers a cote de Installer.bat.
echo Si vous l'avez lance depuis l'archive .zip, extrayez d'abord toute l'archive
echo (clic droit sur le .zip, "Extraire tout"), puis relancez Installer.bat depuis le dossier extrait.
pause
exit /b 1
