@echo off
rem ============================================================
rem ProgressCore-Engine launcher (no Visual Studio needed)
rem
rem Runs the built exe with the repo root as working directory.
rem The engine loads Assets/Shader/... via relative paths, so
rem double-clicking the exe directly (cwd = Build\bin\x64\<Config>\)
rem would fail to find assets. This script fixes the cwd first.
rem ============================================================

cd /d "%~dp0"

set CONFIG=Debug
if not "%~1"=="" set CONFIG=%~1

set EXE=Build\bin\x64\%CONFIG%\ProgressEngine.exe

if not exist "%EXE%" (
    echo [Run.bat] Not found: %EXE%
    echo   Usage: Run.bat [Debug^|Development^|Release]
    echo   Build that configuration in Visual Studio first.
    pause
    exit /b 1
)

echo [Run.bat] Launching %CONFIG%: %EXE%
start "" "%EXE%"
