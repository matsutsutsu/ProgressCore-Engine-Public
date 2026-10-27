@echo off
chcp 65001 > nul

rem ---------------------------------------------------------------------
rem  Data.pak builder (launcher for make_pak.py)
rem
rem  Packs Assets/ into Build/Pak/Data.pak for the Shipping build.
rem
rem  NOTE: this .bat must stay pure ASCII. cmd.exe mis-parses batch files
rem  that contain multibyte characters, so all Japanese docs live in the
rem  .py and the .txt instead.
rem
rem  Other options - run the .py directly:
rem      python make_pak.py --dry-run
rem      python make_pak.py --list
rem ---------------------------------------------------------------------

python "%~dp0make_pak.py" %*

echo.
pause
