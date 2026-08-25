# =============================================================================
#  テクスチャ DDS 変換ツール
#
#  Assets/Models 以下の PNG / JPEG を、BC7 圧縮 + フルミップチェーン付きの
#  DDS に変換して同じフォルダへ書き出す。
#
#  なぜ必要か:
#    ModelResource::BuildModel は WIC で PNG/JPEG をデコードしてから
#    RGBA8 に変換して GPU へ上げている。4K テクスチャが数十枚あると
#    Debug ビルドでシーンロードが 60 秒近くかかる（実測値）。
#    DDS なら読み込みが実質 memcpy になり、ミップも付くので描画も速くなる。
#
#    エンジン側は ModelResource::BuildModel が「同名の .dds があればそちらを
#    優先」する実装になっているため、元の PNG/JPEG はそのまま残してよい。
#    .cereal キャッシュや .glb を作り直す必要も無い。
#
#  使い方:
#    テクスチャDDS変換.bat をダブルクリック（既定: 最大2048、既存.ddsはスキップ）
#    もしくは PowerShell から:
#      .\TextureToDDS.ps1 -MaxSize 2048
#      .\TextureToDDS.ps1 -MaxSize 0        # 縮小せず元の解像度のまま
#      .\TextureToDDS.ps1 -Root Assets\Models\Building
#      .\TextureToDDS.ps1 -Force            # 既存の .dds も作り直す
#      .\TextureToDDS.ps1 -ExcludeDirs @()   # Test フォルダも含めて全部変換
#      .\TextureToDDS.ps1 -DryRun           # 変換せず対象一覧だけ出す
#
#  元に戻したいとき:
#    生成された .dds を消せば PNG/JPEG 読み込みに戻る（追加のみで破壊はしない）。
# =============================================================================

[CmdletBinding()]
param(
    # 長辺の上限ピクセル数。0 を指定すると縮小しない。
    [int]    $MaxSize = 2048,

    # 走査するルート。リポジトリルートからの相対パス。
    [string] $Root    = 'Assets\Models',

    # 走査から除外するフォルダ名（パスにこの名前が含まれるものを飛ばす）。
    # 既定で Test を除外している理由:
    #   Assets\Models\Test は Sponza / DamagedHelmet 等の glTF サンプル置き場で、
    #   ゲームシーンからは参照されていない。しかもテクスチャ名が
    #   "10381718147657362067.jpg" のようなハッシュ名で、下の sRGB 判定
    #   （接尾辞ベース）が効かず BaseColor をリニア扱いして色が転ぶ。
    #   必要なら -ExcludeDirs @() で除外を解除できる。
    [string[]] $ExcludeDirs = @('Test'),

    # 既に .dds があっても作り直す。
    [switch] $Force,

    # 変換せず、何がどう変換されるかだけ表示する。
    [switch] $DryRun
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# このスクリプトは Tools\batツール\ に置かれている前提でリポジトリルートへ移動する
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Set-Location $repoRoot

$texconv = Join-Path $repoRoot 'Tools\Texconv\texconv.exe'
if (-not (Test-Path $texconv)) {
    Write-Host "texconv.exe が見つかりません: $texconv" -ForegroundColor Red
    exit 1
}

$srcRoot = Join-Path $repoRoot $Root
if (-not (Test-Path $srcRoot)) {
    Write-Host "変換対象フォルダが見つかりません: $srcRoot" -ForegroundColor Red
    exit 1
}

# -----------------------------------------------------------------------------
# 色空間について：全て BC7_UNORM で出す（_SRGB は使わない）
#
#   このエンジンには sRGB の概念が一切無い。
#     - スワップチェーンは DXGI_FORMAT_R8G8B8A8_UNORM（GraphicsDevice.cpp）
#     - Source/Engine/Graphics 全体で "SRGB" の出現数は 0
#     - シェーダ側にもガンマ補正（pow 2.2 等）は無い
#   つまり sRGB エンコードされたテクスチャのバイト列をそのままリニア値として
#   読み、ライティングして、変換せずバックバッファへ書いている。物理的に正しい
#   カラーマネジメントではないが、パイプライン全体で一貫しており、アートは
#   その前提で調整されている。
#
#   ここで BaseColor を BC7_UNORM_SRGB にすると、これまで存在しなかった
#   ハード側の sRGB→リニア変換がサンプリング時に入り、アルベドが一段暗く沈む
#   （0.5 -> 0.21）。結果、赤茶けた「錆びたような」色になる。実際にそうなった。
#
#   したがって WIC 読み込み時（R8G8B8A8_UNORM 扱い）と完全に同じ解釈になる
#   BC7_UNORM で統一する。ピクセル値の解釈は変わらず、BC7圧縮とミップだけが
#   追加される。
#
#   将来エンジンを本物のリニアワークフローに直す場合は、バックバッファと
#   BaseColor/Emissive の両方を同時に _SRGB 化すること。片方だけでは破綻する。
# -----------------------------------------------------------------------------

# 長辺が $MaxSize を超えるときだけ、アスペクト比を保って縮小サイズを返す。
# BC7 は 4x4 ブロック圧縮なので 4 の倍数に丸める。
function Get-TargetSize([int] $w, [int] $h, [int] $max) {
    if ($max -le 0)                      { return $null }
    if ($w -le $max -and $h -le $max)    { return $null }

    $scale = [double] $max / [math]::Max($w, $h)
    $nw    = [math]::Max(4, [int]([math]::Round($w * $scale / 4.0)) * 4)
    $nh    = [math]::Max(4, [int]([math]::Round($h * $scale / 4.0)) * 4)
    return @($nw, $nh)
}

# System.Drawing にファイルを掴ませたままにしないよう、必ずストリーム経由で開く。
function Get-ImageSize([string] $path) {
    $stream = [System.IO.File]::OpenRead($path)
    try {
        $image = [System.Drawing.Image]::FromStream($stream, $false, $false)
        try   { return @($image.Width, $image.Height) }
        finally { $image.Dispose() }
    }
    finally { $stream.Dispose() }
}

# -----------------------------------------------------------------------------
# 対象収集
# -----------------------------------------------------------------------------
$targets = Get-ChildItem -Path $srcRoot -Recurse -File -Include *.png, *.jpg, *.jpeg

if ($ExcludeDirs.Count -gt 0) {
    $targets = $targets | Where-Object {
        $relative = $_.FullName.Substring($srcRoot.Length)
        $segments = $relative.Split([System.IO.Path]::DirectorySeparatorChar)
        -not ($segments | Where-Object { $ExcludeDirs -contains $_ })
    }
}

Write-Host ''
Write-Host '=== テクスチャ DDS 変換 ===' -ForegroundColor Cyan
Write-Host ("  対象ルート : {0}" -f $srcRoot)
Write-Host ("  長辺上限   : {0}" -f $(if ($MaxSize -le 0) { '縮小しない' } else { "$MaxSize px" }))
Write-Host ("  既存.dds   : {0}" -f $(if ($Force) { '上書きする' } else { 'スキップする' }))
Write-Host ("  除外       : {0}" -f $(if ($ExcludeDirs.Count -gt 0) { $ExcludeDirs -join ', ' } else { 'なし' }))
Write-Host ("  候補       : {0} 枚" -f $targets.Count)
Write-Host ''

$converted = 0
$skipped   = 0
$failed    = 0
$srcBytes  = [long] 0
$dstBytes  = [long] 0
$index     = 0

foreach ($file in $targets) {
    $index++
    $ddsPath = [System.IO.Path]::ChangeExtension($file.FullName, '.dds')

    if ((Test-Path $ddsPath) -and (-not $Force)) {
        $skipped++
        continue
    }

    # 上のコメントの通り、色空間による振り分けはしない。全て BC7_UNORM。
    $format = 'BC7_UNORM'

    try   { $size = Get-ImageSize $file.FullName }
    catch {
        Write-Host ("  [{0}/{1}] 画像を読めませんでした: {2}" -f $index, $targets.Count, $file.Name) -ForegroundColor Yellow
        $failed++
        continue
    }

    $resize   = Get-TargetSize $size[0] $size[1] $MaxSize
    $sizeArgs = @()
    $sizeNote = "$($size[0])x$($size[1])"
    if ($null -ne $resize) {
        $sizeArgs = @('-w', $resize[0], '-h', $resize[1])
        $sizeNote = "$($size[0])x$($size[1]) -> $($resize[0])x$($resize[1])"
    }

    Write-Host ("  [{0}/{1}] {2}  ({3}, {4})" -f $index, $targets.Count, $file.Name, $sizeNote, $format)

    if ($DryRun) { continue }

    # -m 0 = フルミップチェーン生成。-y = 既存出力を上書き。
    $argList = @('-nologo', '-y', '-f', $format, '-m', '0') + $sizeArgs +
               @('-o', $file.DirectoryName, $file.FullName)

    & $texconv @argList | Out-Null

    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $ddsPath)) {
        Write-Host ("      変換に失敗しました (exit={0})" -f $LASTEXITCODE) -ForegroundColor Red
        $failed++
        continue
    }

    $converted++
    $srcBytes += $file.Length
    $dstBytes += (Get-Item $ddsPath).Length
}

Write-Host ''
Write-Host '=== 完了 ===' -ForegroundColor Cyan
Write-Host ("  変換     : {0} 枚" -f $converted)
Write-Host ("  スキップ : {0} 枚 (既に .dds あり)" -f $skipped)
if ($failed -gt 0) {
    Write-Host ("  失敗     : {0} 枚" -f $failed) -ForegroundColor Red
}
if ($converted -gt 0) {
    Write-Host ("  容量     : {0:N1} MB (元) -> {1:N1} MB (DDS)" -f ($srcBytes / 1MB), ($dstBytes / 1MB))
}
Write-Host ''
Write-Host '元の PNG/JPEG は残してあります。エンジンは同名の .dds を自動優先します。' -ForegroundColor DarkGray
Write-Host '戻したい場合は生成された .dds を削除してください。' -ForegroundColor DarkGray
Write-Host ''
