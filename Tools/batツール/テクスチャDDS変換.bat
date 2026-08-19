@echo off
chcp 65001 > nul

rem ---------------------------------------------------------------------
rem  Texture DDS Converter (launcher for TextureToDDS.ps1)
rem
rem  Converts PNG/JPEG under Assets/Models into BC7 + mipmapped DDS,
rem  so that scene loading stops paying for WIC decode at runtime.
rem  Default: longest edge capped at 2048, existing .dds are skipped.
rem
rem  NOTE: this .bat must stay pure ASCII. cmd.exe mis-parses batch files
rem  that contain multibyte characters, so all Japanese docs live in the
rem  .ps1 instead.
rem
rem  Other options - run the .ps1 directly from PowerShell:
rem      .\TextureToDDS.ps1 -MaxSize 0
rem      .\TextureToDDS.ps1 -Root Assets\Models\Building
rem      .\TextureToDDS.ps1 -DryRun
rem      .\TextureToDDS.ps1 -Force
rem ---------------------------------------------------------------------

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0TextureToDDS.ps1" -MaxSize 2048

echo.
pause
