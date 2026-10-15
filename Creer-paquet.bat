@echo off
rem Construit le paquet de distribution de XR Perf Overlay (programme deja compile, sans les sources) :
rem   release\XRPerfOverlay-<version>-win64.zip
rem Il s'installe ensuite sur n'importe quel PC par un double-clic sur Installer.bat, sans Visual Studio
rem ni SDK .NET. Le numero de version vient du fichier VERSION.
rem Il faut ici les memes outils que pour build.bat, et un acces a Internet (le runtime .NET inclus
rem dans le paquet est telecharge par NuGet).
setlocal
cd /d "%~dp0"
echo ============================================================
echo  XR Perf Overlay - paquet de distribution
echo ============================================================
echo.
set "XRPO_DIST=build\package"
if exist "%XRPO_DIST%" rmdir /s /q "%XRPO_DIST%"
echo Etape 1 sur 2 : compilation
call "%~dp0build.bat" --auto
if errorlevel 1 goto :fail
echo.
echo Etape 2 sur 2 : paquet
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package.ps1"
if errorlevel 1 goto :fail
echo.
pause
exit /b 0

:fail
echo.
echo Le paquet n'a pas pu etre construit. Le detail est ci-dessus (et dans build.log pour la compilation).
pause
exit /b 1
