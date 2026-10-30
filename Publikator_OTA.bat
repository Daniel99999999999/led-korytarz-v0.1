@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Publikator firmware OTA - Sterownik LED

if not exist "scripts\publikator_z_build.py" (
 echo BLAD: Nie znaleziono scripts\publikator_z_build.py
 echo Rozpakuj caly pakiet do katalogu glownego projektu LED.
 pause
 exit /b 1
)

set "PYTHON_EXE="
set "PYTHON_ARGS="
where py >nul 2>nul
if not errorlevel 1 (
  py -3 -c "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>nul
  if not errorlevel 1 (
    set "PYTHON_EXE=py"
    set "PYTHON_ARGS=-3"
  )
)
if not defined PYTHON_EXE (
  where python >nul 2>nul
  if not errorlevel 1 set "PYTHON_EXE=python"
)
if not defined PYTHON_EXE if exist "%USERPROFILE%\.platformio\penv\Scripts\python.exe" set "PYTHON_EXE=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if not defined PYTHON_EXE (
 echo BLAD: Nie znaleziono Pythona 3.10+. Zainstaluj Python lub PlatformIO.
 pause
 exit /b 1
)

:menu
cls
echo =======================================================
echo       PUBLIKATOR OTA - STEROWNIK LED ESP32
echo =======================================================
echo Profil: esp32doit-devkit-v1-ota
echo Katalog: %CD%
echo.
echo  [0] Przygotuj nowa wersje i ustaw PROJECT_VER (PRZED BUILD)
echo  [1] Publikuj gotowy Build na GitHub (bez OTA)
echo  [2] Publikuj gotowy Build + uruchom OTA przez MQTT
echo  [3] Ponow OTA istniejacego wydania (bez publikacji)
echo  [4] Wygeneruj YAML do recznego uruchomienia z HA
echo  [5] Zakoncz
echo.
choice /C 012345 /N /M "Wybierz 0-5: "
if errorlevel 6 goto :end
if errorlevel 5 (set "OPERATION=ha-yaml" & goto :run)
if errorlevel 4 (set "OPERATION=install-only" & goto :run)
if errorlevel 3 (set "OPERATION=publish-install" & goto :run)
if errorlevel 2 (set "OPERATION=publish-only" & goto :run)
if errorlevel 1 (set "OPERATION=prepare-version" & goto :run)

:run
echo.
echo Wybrano: %OPERATION%
echo.
"%PYTHON_EXE%" %PYTHON_ARGS% "scripts\publikator_z_build.py" %OPERATION%
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
 echo ==============================================
 echo  OPERACJA ZAKONCZONA POMYSLNIE
 echo ==============================================
) else (
 echo ==============================================
 echo  BLAD - kod: %RC%. Zobacz szczegoly powyzej.
 echo ==============================================
)
echo.
pause
goto :menu

:end
endlocal
exit /b 0
