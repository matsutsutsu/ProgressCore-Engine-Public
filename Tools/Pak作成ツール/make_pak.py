# =============================================================================
#  Data.pak 作成ツール
#
#  Assets 以下のファイルを 1 つのアーカイブ（Data.pak）にまとめる。
#  Shipping ビルドのエンジンは起動時に Data.pak を開き、そこからアセットを読む。
#
#  書式は Source/Engine/Platform/Core/FileSystem.cpp と揃えること（数値はリトルエンディアン）:
#    [ヘッダ]  "PCPK"(4バイト) / version(u32) / entryCount(u32) / flags(u32)
#    [目次]    entryCount 回: pathLength(u16) / path(UTF-8) / offset(u64) / size(u64)
#    [中身]    各ファイルのバイト列。offset はアーカイブの先頭からの位置
#
#  使い方:
#    Pak作成.bat をダブルクリック（Build/Pak/Data.pak に書き出す）
#    もしくは:
#      python make_pak.py              # 作る
#      python make_pak.py --dry-run    # 作らずに、入れる物・外す物の集計だけ出す
#      python make_pak.py --list       # 入れるファイルを 1 行ずつ全部出す
#      python make_pak.py --out D:/x/Data.pak
# =============================================================================

import argparse
import shutil
import struct
import sys
from pathlib import Path

MAGIC = b"PCPK"
VERSION = 1
FLAGS = 0  # 0 = 暗号化なし（段4で使う予定）

# このスクリプトは Tools/Pak作成ツール/ に置かれている前提
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
ASSETS_DIR = "Assets"
DEFAULT_OUT = REPO_ROOT / "Build" / "Pak" / "Data.pak"

# ---- 入れない物 -------------------------------------------------------------
# Sponza などの glTF サンプル置き場。ゲームからは使っていない
EXCLUDE_DIRS = ["Assets/Models/Test"]

# ArialUni はエディタ（ImGui）専用で、配布物に同梱できないフォント
EXCLUDE_FILES = ["Assets/Fonts/ArialUni.ttf"]

# モデルのテクスチャは、同名の .dds があればエンジンがそちらを読む（ModelResource::BuildModel）。
# 元の画像は使われないので入れない。モデル以外（UI など）は .png を名指しで読むので対象外
IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".tga", ".bmp"}

# モデルは同名の .cereal があればエンジンがそちらを読む（ModelResource::Load）。
# 元の .glb / .gltf / .bin は使われないので入れない
MODEL_SOURCE_EXTS = {".glb", ".gltf", ".bin"}


def normalize(path: str) -> str:
    """パスの表記を揃える（\\ を / に、. と .. を解決）。FileSystem.cpp の NormalizePath と同じ処理。"""
    parts = []
    for part in path.replace("\\", "/").split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            if parts and parts[-1] != "..":
                parts.pop()
            else:
                parts.append("..")
            continue
        parts.append(part)
    return "/".join(parts)


def lower_ascii(text: str) -> str:
    """英字だけを小文字にする（エンジン側も ASCII だけを小文字にして引くため）。"""
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in text)


def exclusion_reason(rel: str, file: Path):
    """入れないファイルなら理由を返す。入れるなら None。"""
    lower = lower_ascii(rel)

    for d in EXCLUDE_DIRS:
        if lower.startswith(lower_ascii(d) + "/"):
            return "Test フォルダ"
    if lower in (lower_ascii(f) for f in EXCLUDE_FILES):
        return "同梱しないフォント"

    ext = file.suffix.lower()
    if lower.startswith("assets/models/") and ext in IMAGE_EXTS and file.with_suffix(".dds").exists():
        return "同名の .dds がある元画像"
    if ext in MODEL_SOURCE_EXTS and file.with_suffix(".cereal").exists():
        return "同名の .cereal がある元モデル"
    return None


def collect():
    """Assets 以下を走査して、入れる物と外す物に分ける。"""
    included = []  # (rel, file, size)
    excluded = {}  # reason -> [count, bytes]

    for file in sorted((REPO_ROOT / ASSETS_DIR).rglob("*")):
        if not file.is_file():
            continue
        rel = normalize(file.relative_to(REPO_ROOT).as_posix())
        size = file.stat().st_size

        reason = exclusion_reason(rel, file)
        if reason:
            stat = excluded.setdefault(reason, [0, 0])
            stat[0] += 1
            stat[1] += size
            continue
        included.append((rel, file, size))

    return included, excluded


def check_entries(included):
    """エンジンが引けない目次になっていないか確かめる。問題があれば理由の一覧を返す。"""
    errors = []
    seen = {}
    for rel, _, _ in included:
        encoded = rel.encode("utf-8")
        if len(encoded) > 0xFFFF:
            errors.append(f"パスが長すぎます: {rel}")
        key = lower_ascii(rel)
        if key in seen:
            # エンジンは小文字にして引くので、大文字小文字だけ違う2つは区別できない
            errors.append(f"大文字小文字だけが違うファイルがあります: {seen[key]} / {rel}")
        seen[key] = rel
    return errors


def write_pak(included, out_path: Path):
    """目次を先に組み立ててから中身を流し込む。途中で失敗しても壊れた Data.pak を残さないよう一時ファイルに書く。"""
    paths = [rel.encode("utf-8") for rel, _, _ in included]
    header_size = len(MAGIC) + 4 * 3
    table_size = sum(2 + len(p) + 8 + 8 for p in paths)

    # 各ファイルの位置を決める
    offset = header_size + table_size
    table = bytearray()
    for p, (_, _, size) in zip(paths, included):
        table += struct.pack("<H", len(p)) + p + struct.pack("<QQ", offset, size)
        offset += size

    out_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = out_path.with_suffix(out_path.suffix + ".tmp")
    with open(tmp_path, "wb") as out:
        out.write(MAGIC + struct.pack("<III", VERSION, len(included), FLAGS))
        out.write(table)
        for _, file, _ in included:
            with open(file, "rb") as src:
                shutil.copyfileobj(src, out, 1024 * 1024)
    tmp_path.replace(out_path)


def mb(n: int) -> str:
    return f"{n / (1024 * 1024):,.1f} MB"


def main():
    parser = argparse.ArgumentParser(description="Assets を Data.pak にまとめる")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="書き出し先（既定: Build/Pak/Data.pak）")
    parser.add_argument("--dry-run", action="store_true", help="作らずに集計だけ出す")
    parser.add_argument("--list", action="store_true", help="入れるファイルを全部出す")
    args = parser.parse_args()

    included, excluded = collect()

    print()
    print("=== Data.pak 作成 ===")
    print(f"  入れる : {len(included)} ファイル / {mb(sum(s for _, _, s in included))}")
    for reason, (count, size) in excluded.items():
        print(f"  外す   : {count} ファイル / {mb(size)}  ({reason})")
    print()

    if args.list:
        for rel, _, size in included:
            print(f"  {rel}  ({mb(size)})")
        print()

    errors = check_entries(included)
    if errors:
        for e in errors:
            print(f"  [エラー] {e}")
        print("作成を中止しました。")
        return 1

    if args.dry_run:
        print("--dry-run なので作成していません。")
        return 0

    write_pak(included, args.out)
    print(f"書き出しました: {args.out}  ({mb(args.out.stat().st_size)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
