@echo off
rem ===========================================================================
rem ECSPlayground Build.bat
rem ---------------------------------------------------------------------------
rem Builds the CCL ECS core as a standalone console app.
rem No DirectX, no Jolt, no ImGui, no pch.h.
rem
rem Sources: the 5 ECS .cpp files + ECSPlayground.cpp. That is all.
rem
rem Key requirements (see README.md for the full Japanese explanation):
rem   - Do NOT define ENABLE_EDITOR -> drops the ImGui dependency in
rem     CCL_SystemManager.cpp (all of it sits inside #ifdef ENABLE_EDITOR).
rem   - Do NOT define TRACY_ENABLE  -> all Tracy macros compile to nothing.
rem   - Put Stub\ BEFORE Source\ on the include path -> the dead
rem     ShapeRenderer.h include in CCL_World.cpp:3 resolves to our empty stub,
rem     so the engine sources stay untouched.
rem
rem NOTE: keep this file ASCII-only. cmd.exe reads .bat in the ANSI codepage,
rem       so UTF-8 Japanese comments corrupt the parsing.
rem ===========================================================================

setlocal

rem --- repo root = two levels above this script ---
set "ROOT=%~dp0..\.."
set "OUT=%~dp0Bin"

rem --- Load the VS2026 toolchain ---
rem   VS2022's MSBuild triggers a std::align_val_t C2011 header clash, so this
rem   project standardises on VS2026.
set "VCVARS=D:\01_Programs\VisualStudio2026\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvarsall.bat not found: %VCVARS%
    exit /b 1
)
rem   -vcvars_ver=14.38 pins the v143 toolset, which is what ProgressEngine.vcxproj
rem   uses (<PlatformToolset>v143</PlatformToolset>). The default toolset on this
rem   machine is v145 (14.51), whose CRT import libraries are NOT installed --
rem   linking against it fails with "cannot open file 'msvcprtd.lib'".
call "%VCVARS%" x64 -vcvars_ver=14.38 >nul
if errorlevel 1 (
    echo [ERROR] vcvarsall.bat failed
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"

echo.
echo === Building ECSPlayground ===
echo.

cl /nologo /std:c++20 /EHsc /utf-8 /Zc:char8_t- /W3 /Zi /Od /MDd ^
   /I "%~dp0Stub" ^
   /I "%ROOT%\Source" ^
   /I "%ROOT%\External\Tracy" ^
   /Fe:"%OUT%\ECSPlayground.exe" ^
   /Fo:"%OUT%\\" ^
   /Fd:"%OUT%\ECSPlayground.pdb" ^
   "%~dp0ECSPlayground.cpp" ^
   "%ROOT%\Source\Engine\ECS\Core\CCL_Chunk.cpp" ^
   "%ROOT%\Source\Engine\ECS\Core\CCL_ChunkManager.cpp" ^
   "%ROOT%\Source\Engine\ECS\Core\CCL_World.cpp" ^
   "%ROOT%\Source\Engine\ECS\Jobs\CCL_JobSystem.cpp" ^
   "%ROOT%\Source\Engine\ECS\System\CCL_SystemManager.cpp" ^
   "%ROOT%\External\Tracy\TracyClient.cpp" ^
   /link /SUBSYSTEM:CONSOLE

if errorlevel 1 (
    echo.
    echo [FAILED] build error
    exit /b 1
)

echo.
echo [OK] %OUT%\ECSPlayground.exe
echo.
endlocal
