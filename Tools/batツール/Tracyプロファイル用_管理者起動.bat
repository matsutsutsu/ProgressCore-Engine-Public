@echo off
rem ============================================================================
rem  Tracy プロファイル用：OVERCLEAVE(Development) を「管理者権限」で起動する。
rem
rem  なぜ管理者が要るか:
rem    Tracy のサンプリング/コンテキストスイッチ(スレッドが実行中か待機中か、
rem    赤=カーネル時間 の色分け)は Windows の ETW カーネルセッションを使う。
rem    これは管理者権限でないと開けないため、通常起動だと「穴の色分け」が出ない。
rem
rem  使い方:
rem    1) このバットを実行（UAC が出たら「はい」）
rem    2) 別途 Tools\Tracy\tracy-profiler.exe を起動し Connect
rem       → Simulation/Update/Render の帯と、ワーカーの待機(赤)が見える
rem
rem  ※ Development 構成は TRACY_ENABLE 有効（プロファイル向けの最適化ビルド）。
rem ============================================================================

setlocal

rem このバットの位置(...\Tools\batツール\)から見たリポジトリ直下を作業ディレクトリにする。
rem （ゲームは Assets\ 等を「カレント=リポジトリ直下」からの相対パスで開くため）
set "REPO_ROOT=%~dp0..\.."
set "EXE=%REPO_ROOT%\Build\bin\x64\Development\OVERCLEAVE.exe"

if not exist "%EXE%" (
    echo [ERROR] 実行ファイルが見つかりません: "%EXE%"
    echo         先に Development 構成でビルドしてください。
    pause
    exit /b 1
)

rem PowerShell 経由で「管理者昇格(RunAs)」しつつ、作業ディレクトリをリポジトリ直下に固定して起動。
powershell -NoProfile -Command ^
  "Start-Process -FilePath '%EXE%' -WorkingDirectory (Resolve-Path '%REPO_ROOT%').Path -Verb RunAs"

endlocal
