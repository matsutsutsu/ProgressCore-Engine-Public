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
#  暗号化（既定で有効。flags = 1）:
#    ヘッダより後ろ（目次と中身）の全バイトに、乱数列を XOR する。ヘッダは平文のまま。
#    位置 p のバイトには「splitmix(KEY ^ (p // 8)) の p % 8 番目のバイト」を XOR する。
#    乱数列は位置だけで決まるので、エンジンはどのファイルからでもその場で戻せる。
#
#  使い方:
#    Pak作成.bat をダブルクリック（Build/Pak/Data.pak に書き出す）
#    もしくは:
#      python make_pak.py              # 作る
#      python make_pak.py --dry-run    # 作らずに、入れる物・外す物の集計だけ出す
#      python make_pak.py --list       # 入れるファイルを 1 行ずつ全部出す
#      python make_pak.py --no-encrypt # 暗号化しないで作る（中身を目で確かめたいとき）
#      python make_pak.py --out D:/x/Data.pak
# =============================================================================

import argparse
import struct
import sys
from pathlib import Path

MAGIC = b"PCPK"
VERSION = 1
FLAG_XOR = 1 << 0  # 目次と中身を XOR してある

# 乱数列の種。FileSystem.cpp の kArchiveKey と同じ値にすること
KEY = 0x5A3C9E71D24B8F06

U64_MASK = (1 << 64) - 1

# このスクリプトは Tools/Pak作成ツール/ に置かれている前提
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
ASSETS_DIR = "Assets"
DEFAULT_OUT = REPO_ROOT / "Build" / "Pak" / "Data.pak"

# ---- 入れない物 -------------------------------------------------------------
# Sponza などの glTF サンプル置き場。ゲームからは使っていない
EXCLUDE_DIRS = ["Assets/Models/Test"]

# 音の Unused フォルダは使わない候補の置き場。エンジンも読まない（DxtkAudioManager::LoadSoundFolder）
UNUSED_AUDIO_DIR_NAME = "unused"

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


def splitmix(x: int) -> int:
    """64bit の値をよく混ぜる（splitmix64）。FileSystem.cpp の SplitMix64 と同じ計算。
    Python の整数は桁あふれしないので、掛け算のたびに 64bit に切り詰める。"""
    z = (x + 0x9E3779B97F4A7C15) & U64_MASK
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & U64_MASK
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & U64_MASK
    return z ^ (z >> 31)


def xor_at(data: bytes, position: int) -> bytes:
    """data（アーカイブ先頭から position バイト目に置くもの）に乱数列を XOR する。FileSystem.cpp の XorArchiveBytes と同じ結果になる。"""
    if not data:
        return data
    first_block = position >> 3
    last_block = (position + len(data) + 7) >> 3
    stream = struct.pack(f"<{last_block - first_block}Q",
                         *(splitmix(KEY ^ block) for block in range(first_block, last_block)))
    start = position - first_block * 8
    stream = stream[start:start + len(data)]
    # 1 バイトずつ回すと遅いので、まとめて大きな整数にして一度に XOR する
    mixed = int.from_bytes(data, "little") ^ int.from_bytes(stream, "little")
    return mixed.to_bytes(len(data), "little")


def lower_ascii(text: str) -> str:
    """英字だけを小文字にする（エンジン側も ASCII だけを小文字にして引くため）。"""
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in text)


def exclusion_reason(rel: str, file: Path):
    """入れないファイルなら理由を返す。入れるなら None。"""
    lower = lower_ascii(rel)

    for d in EXCLUDE_DIRS:
        if lower.startswith(lower_ascii(d) + "/"):
            return "Test フォルダ"
    if lower.startswith("assets/audio/") and "/" + UNUSED_AUDIO_DIR_NAME + "/" in lower:
        return "使わない音（Unused フォルダ）"
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


def write_pak(included, out_path: Path, encrypt: bool):
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

    # 暗号化するときは、書く位置（アーカイブ先頭から何バイト目か）で乱数列を決めるので、位置を数えながら書く
    def write(out, data: bytes, position: int) -> int:
        out.write(xor_at(data, position) if encrypt else data)
        return position + len(data)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = out_path.with_suffix(out_path.suffix + ".tmp")
    with open(tmp_path, "wb") as out:
        flags = FLAG_XOR if encrypt else 0
        out.write(MAGIC + struct.pack("<III", VERSION, len(included), flags))  # ヘッダは平文
        position = write(out, bytes(table), header_size)
        for _, file, _ in included:
            with open(file, "rb") as src:
                while chunk := src.read(1024 * 1024):
                    position = write(out, chunk, position)
    tmp_path.replace(out_path)


def mb(n: int) -> str:
    return f"{n / (1024 * 1024):,.1f} MB"


def main():
    parser = argparse.ArgumentParser(description="Assets を Data.pak にまとめる")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="書き出し先（既定: Build/Pak/Data.pak）")
    parser.add_argument("--dry-run", action="store_true", help="作らずに集計だけ出す")
    parser.add_argument("--list", action="store_true", help="入れるファイルを全部出す")
    parser.add_argument("--no-encrypt", action="store_true", help="暗号化しないで作る")
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

    encrypt = not args.no_encrypt
    print("書き出し中..." + ("（暗号化あり。数十秒かかります）" if encrypt else "（暗号化なし）"))
    write_pak(included, args.out, encrypt)
    print(f"書き出しました: {args.out}  ({mb(args.out.stat().st_size)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
