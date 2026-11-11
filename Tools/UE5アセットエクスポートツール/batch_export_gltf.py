# -*- coding: utf-8 -*-
"""
================================================================================
 glTF / GLB 一括エクスポートツール  (Unreal Engine 5.7 / Python)
================================================================================

 機能概要:
   Content Browser で選択した StaticMesh / SkeletalMesh / AnimSequence を
   glTF(.gltf) または GLB(.glb) へ、SoundWave を .wav へ一括エクスポートする。
   出力先は実行するたびにダイアログで選ぶ。
   エクスポート後、出力ファイルの中身を検査して Output Log に報告する（glTF のみ）。

 前提:
   - Python Editor Script Plugin 有効
   - glTF Exporter Plugin 有効（UE5.1 以降はエンジン同梱）

 使い方:
   1. Content Browser で対象アセットを複数選択
   2. 本スクリプトを実行（メニュー or [py "パス"] or Python コンソール）
   3. フォルダ選択ダイアログで出力先を選ぶ
   4. 進捗ダイアログが表示され、エクスポートが進む（Cancel で中断可能）
   5. 完了後、Output Log に検査結果、結果ダイアログに Success / Failed 件数を表示

 使用している公式 API:
   - unreal.EditorUtilityLibrary.get_selected_assets()
   - unreal.GLTFExporter.export_to_gltf()
   - unreal.GLTFExportOptions
   - unreal.AssetExportTask / unreal.SoundExporterWAV / unreal.Exporter.run_asset_export_task()（音声）
   - unreal.ScopedSlowTask
   - unreal.EditorDialog（結果表示。失敗しても処理は継続）
================================================================================
"""

import os
import json
import struct
from collections import Counter

import unreal


# ==============================================================================
#  設定
# ==============================================================================

# 出力形式:
#   False -> .gltf （JSON + .bin とテクスチャが同フォルダに分割出力）
#   True  -> .glb  （1ファイルに全部入りのバイナリ）
EXPORT_GLB = True

# 出力先フォルダの中での置き方:
#   True  -> 選んだフォルダの直下へ平置き
#   False -> UE のフォルダ構造をそのまま再現する
#
# エンジンへ入れる場合は True にすること。
# エンジンはアニメーションフォルダの「直下のみ」を走査する（非再帰）ため、
# 構造を再現するとサブフォルダへ掘られて発見されない。
FLATTEN_OUTPUT = True

# AnimSequence を出力する際に、プレビューメッシュも一緒に出力するかどうか。
# ボーン情報はメッシュ無しでも含まれるので、エンジンへ入れる用途では False。
# 単体で別ソフトに読ませて見た目を確認したいときだけ True。
EXPORT_PREVIEW_MESH = False

# エクスポート後に出力ファイルを開いて中身を検査するか。
# ノード数・アニメーション名・キー数分布を Output Log に出す。
INSPECT_AFTER_EXPORT = True

# 照合用モデル（フルパス）。出したアニメーションのボーン名がこのモデルと
# 一致するかを確認する。
# 空にしておくと、選んだ出力先の親フォルダにある .glb を自動で探す。
REFERENCE_MODEL = ""


# ==============================================================================
#  定数
# ==============================================================================

# 対応アセット型。これ以外（Texture, Material, Blueprint 等）は無視する。
SUPPORTED_TYPES = (
    unreal.StaticMesh,
    unreal.SkeletalMesh,
    unreal.AnimSequence,
    unreal.SoundWave,   # glTF ではなく .wav で出す（export_sound_wave）
)

# ログの先頭に付けるプレフィックス（Output Log で grep しやすくするため）
LOG_TAG = "[glTF Export] "

# キー数がこれ以下のチャンネルは「潰れている」とみなして警告する。
# 往復運動（開いて閉じる等）は始点と終点が同じ値になるため、
# 2キーに圧縮されると再生しても完全に静止する。
COLLAPSED_KEY_THRESHOLD = 2

# 最後に選んだ出力先を覚えておくファイル。次回のダイアログをそこから開くため。
# __file__ は実行方法によっては定義されないため（exec で流し込んだ場合など）、
# その場合は一時フォルダへ逃がす。
try:
    _SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
except NameError:
    import tempfile
    _SCRIPT_DIR = tempfile.gettempdir()

LAST_DIR_FILE = os.path.join(_SCRIPT_DIR, "last_output_dir.txt")


# ==============================================================================
#  ロギングヘルパー
# ==============================================================================

def log_info(message):
    """Output Log へ通常ログを出力する。"""
    unreal.log(LOG_TAG + message)


def log_warn(message):
    """Output Log へ警告ログを出力する。"""
    unreal.log_warning(LOG_TAG + message)


def log_error(message):
    """Output Log へエラーログを出力する。"""
    unreal.log_error(LOG_TAG + message)


# ==============================================================================
#  フォルダ選択ダイアログ
#    Windows Vista 以降の共通ダイアログ（IFileDialog）を使う。
#    エクスプローラーと同じ見た目で、左のツリー・検索・パス入力が使える。
#
#    旧 API の SHBrowseForFolder は小さなツリーだけの古いダイアログになる。
#    tkinter は UE 同梱 Python に含まれないことが多く当てにできない。
# ==============================================================================

def _make_guid_type():
    import ctypes

    class GUID(ctypes.Structure):
        _fields_ = [("Data1", ctypes.c_ulong),
                    ("Data2", ctypes.c_ushort),
                    ("Data3", ctypes.c_ushort),
                    ("Data4", ctypes.c_ubyte * 8)]

    return GUID


def pick_folder_dialog(title, initial_dir):
    """
    エクスプローラー風のフォルダ選択ダイアログを出す。
    選ばれたパス（str）を返す。キャンセルされた場合は None。
    ダイアログを開けなかった場合は例外を投げる（呼び出し側で中断する）。
    """
    import ctypes
    from ctypes import wintypes

    ole32 = ctypes.windll.ole32
    shell32 = ctypes.windll.shell32
    GUID = _make_guid_type()

    CLSID_FileOpenDialog = "{DC1C5A9C-E88A-4dde-A5A1-60F82A20AEF7}"
    IID_IFileDialog      = "{42f85136-db7e-439c-85f1-e4075d135fc8}"
    IID_IShellItem       = "{43826d1e-e718-42ee-bc55-a1e261c37bfe}"

    CLSCTX_INPROC_SERVER = 1
    FOS_PICKFOLDERS      = 0x00000020   # ファイルではなくフォルダを選ぶ
    FOS_FORCEFILESYSTEM  = 0x00000040   # 実在するパスだけ返させる
    FOS_PATHMUSTEXIST    = 0x00000800

    SIGDN_FILESYSPATH    = 0x80058000
    ERROR_CANCELLED_HR   = 0x800704C7   # ユーザーがキャンセルしたときの HRESULT

    # IFileDialog の vtable 上の位置。
    # IUnknown(3) + IModalWindow(1) + IFileDialog の順に並んでいる。
    IUNK_RELEASE   = 2
    IFD_SHOW       = 3
    IFD_SETOPTIONS = 9
    IFD_GETOPTIONS = 10
    IFD_SETFOLDER  = 12
    IFD_SETTITLE   = 17
    IFD_GETRESULT  = 20
    ISI_GETDISPLAYNAME = 5

    def guid(text):
        g = GUID()
        hr = ole32.CLSIDFromString(text, ctypes.byref(g))
        if hr != 0:
            raise OSError("CLSIDFromString に失敗: 0x%08X" % (hr & 0xFFFFFFFF))
        return g

    def vcall(this, index, restype, argtypes, *args):
        """COM インターフェースの vtable 経由でメソッドを呼ぶ。"""
        vtbl = ctypes.cast(this, ctypes.POINTER(ctypes.c_void_p))[0]
        fn = ctypes.cast(vtbl, ctypes.POINTER(ctypes.c_void_p))[index]
        proto = ctypes.WINFUNCTYPE(restype, ctypes.c_void_p, *argtypes)
        return proto(fn)(this, *args)

    # エディタのスレッドは通常すでに COM 初期化済み。
    # 念のため呼んでおき、自分で初期化した場合だけ後始末する。
    hr_init = ole32.CoInitialize(None)
    needs_uninit = (hr_init == 0)   # S_OK = 自分が初期化した / S_FALSE = すでに済み

    dialog = ctypes.c_void_p()
    try:
        hr = ole32.CoCreateInstance(ctypes.byref(guid(CLSID_FileOpenDialog)), None,
                                    CLSCTX_INPROC_SERVER,
                                    ctypes.byref(guid(IID_IFileDialog)),
                                    ctypes.byref(dialog))
        if hr != 0 or not dialog:
            raise OSError("フォルダ選択ダイアログを作成できません: 0x%08X" % (hr & 0xFFFFFFFF))

        options = wintypes.DWORD()
        vcall(dialog, IFD_GETOPTIONS, ctypes.HRESULT,
              [ctypes.POINTER(wintypes.DWORD)], ctypes.byref(options))
        vcall(dialog, IFD_SETOPTIONS, ctypes.HRESULT, [wintypes.DWORD],
              wintypes.DWORD(options.value | FOS_PICKFOLDERS
                             | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST))

        vcall(dialog, IFD_SETTITLE, ctypes.HRESULT, [wintypes.LPCWSTR], title)

        # 前回の場所から開く（無ければ OS 任せ）
        if initial_dir and os.path.isdir(initial_dir):
            item = ctypes.c_void_p()
            hr = shell32.SHCreateItemFromParsingName(
                wintypes.LPCWSTR(initial_dir), None,
                ctypes.byref(guid(IID_IShellItem)), ctypes.byref(item))
            if hr == 0 and item:
                vcall(dialog, IFD_SETFOLDER, ctypes.HRESULT, [ctypes.c_void_p], item)
                vcall(item, IUNK_RELEASE, ctypes.c_ulong, [])

        # ここでダイアログが開く。閉じるまで戻らない。
        hr = vcall(dialog, IFD_SHOW, ctypes.HRESULT, [wintypes.HWND], None)
        if (hr & 0xFFFFFFFF) == ERROR_CANCELLED_HR:
            return None
        if hr != 0:
            raise OSError("フォルダ選択ダイアログでエラー: 0x%08X" % (hr & 0xFFFFFFFF))

        result_item = ctypes.c_void_p()
        hr = vcall(dialog, IFD_GETRESULT, ctypes.HRESULT,
                   [ctypes.POINTER(ctypes.c_void_p)], ctypes.byref(result_item))
        if hr != 0 or not result_item:
            raise OSError("選択結果を取得できません: 0x%08X" % (hr & 0xFFFFFFFF))

        try:
            path_ptr = ctypes.c_wchar_p()
            hr = vcall(result_item, ISI_GETDISPLAYNAME, ctypes.HRESULT,
                       [ctypes.c_ulong, ctypes.POINTER(ctypes.c_wchar_p)],
                       SIGDN_FILESYSPATH, ctypes.byref(path_ptr))
            if hr != 0 or not path_ptr.value:
                raise OSError("選択されたパスを取得できません: 0x%08X" % (hr & 0xFFFFFFFF))

            path = path_ptr.value
            # GetDisplayName が確保した文字列は呼び出し側が解放する
            ole32.CoTaskMemFree(ctypes.cast(path_ptr, ctypes.c_void_p))
            return path
        finally:
            vcall(result_item, IUNK_RELEASE, ctypes.c_ulong, [])

    finally:
        if dialog:
            vcall(dialog, IUNK_RELEASE, ctypes.c_ulong, [])
        if needs_uninit:
            ole32.CoUninitialize()


# ==============================================================================
#  出力先フォルダの決定
# ==============================================================================

def load_last_output_dir():
    """前回選んだ出力先を返す。無ければ None。"""
    try:
        if os.path.isfile(LAST_DIR_FILE):
            with open(LAST_DIR_FILE, "r", encoding="utf-8") as f:
                path = f.read().strip()
            return path if os.path.isdir(path) else None
    except Exception:
        pass
    return None


def save_last_output_dir(path):
    """選んだ出力先を覚えておく（失敗しても無視する）。"""
    try:
        with open(LAST_DIR_FILE, "w", encoding="utf-8") as f:
            f.write(path)
    except Exception:
        pass


def ask_output_dir():
    """
    出力先をダイアログで選ばせる。
    キャンセルされた場合と開けなかった場合は None を返す（呼び出し側で中断する）。

    勝手にどこかへ出力することはしない。選ばれた場所にだけ出す。
    """
    try:
        picked = pick_folder_dialog("エクスポート先フォルダを選択",
                                    load_last_output_dir())
    except Exception as e:
        log_error("フォルダ選択ダイアログを開けませんでした: {}".format(e))
        return None

    if not picked:
        log_warn("出力先が選ばれなかったため中断しました。")
        return None

    picked = os.path.normpath(picked)
    save_last_output_dir(picked)
    return picked


def resolve_reference_model(output_dir):
    """
    照合用モデルのパスを決める。
    REFERENCE_MODEL が空なら、出力先の親フォルダにある .glb を自動で探す。

    アニメーションを <モデルの場所>/Animations/ へ出す運用を想定している。
    候補が1つに絞れないときは照合しない（間違ったモデルと比べても意味がない）。
    """
    if REFERENCE_MODEL:
        return REFERENCE_MODEL

    parent = os.path.dirname(os.path.normpath(output_dir))
    try:
        candidates = [os.path.join(parent, name) for name in os.listdir(parent)
                      if name.lower().endswith(".glb")]
    except Exception:
        return ""

    if len(candidates) == 1:
        log_info("照合用モデルを自動で見つけました: {}".format(candidates[0]))
        return candidates[0]

    if len(candidates) > 1:
        log_info("親フォルダに .glb が {} 個あるため照合しません。"
                 "指定したい場合は REFERENCE_MODEL に書いてください。".format(len(candidates)))

    return ""


# ==============================================================================
#  アセット判定 / 出力パス構築
# ==============================================================================

def is_supported_asset(asset):
    """対応アセット型かどうかを返す。"""
    return isinstance(asset, SUPPORTED_TYPES)


def is_sound(asset):
    """音声（.wav で出す）かどうかを返す。"""
    return isinstance(asset, unreal.SoundWave)


def output_extension(asset, gltf_extension):
    """アセットごとの拡張子を返す。音声は .wav、それ以外は glTF の拡張子。"""
    return ".wav" if is_sound(asset) else gltf_extension


def build_output_path(asset, output_dir, extension):
    """
    出力ファイルパスを構築する。

    FLATTEN_OUTPUT = True:
        アセット名だけを使って出力先の直下へ置く。
            /Game/Characters/Hero/Anims/Idle -> <出力先>\\Idle.gltf

    FLATTEN_OUTPUT = False:
        パッケージパスのフォルダ構造を維持する。
            /Game/Characters/Hero -> <出力先>\\Characters\\Hero.gltf
        /Game/ 以外のマウント（/Engine/ やプラグインコンテンツ）の場合は
        名前衝突を避けるため、先頭の "/" だけを除いた相対パスにする。
            /MyPlugin/Meshes/Box -> <出力先>\\MyPlugin\\Meshes\\Box.gltf
    """
    if FLATTEN_OUTPUT:
        return os.path.normpath(os.path.join(output_dir, asset.get_name() + extension))

    # get_path_name() は "/Game/Characters/Hero.Hero" 形式を返すため
    # "." より前（= パッケージパス）だけを取り出す。
    # （UE のアセット名に "." は含まれないため split で安全に分離できる）
    package_path = asset.get_path_name().split(".")[0]

    if package_path.startswith("/Game/"):
        relative = package_path[len("/Game/"):]      # 例: "Characters/Hero"
    else:
        relative = package_path.lstrip("/")          # 例: "Engine/..." や "Plugin/..."

    # "/" を OS 区切り文字へ変換してから連結
    relative_os = relative.replace("/", os.sep)
    full_path = os.path.join(output_dir, relative_os) + extension

    # スラッシュ混在などを正規化
    return os.path.normpath(full_path)


def find_output_collisions(targets, output_dir, extension):
    """
    出力パスが重複するアセットを探す。

    平置きでは UE 内の別フォルダにある同名アセットが同じファイルへ書かれてしまい、
    片方が黙って消える。エクスポート前に検出して中断できるようにする。

    戻り値: {出力パス: [アセットのパッケージパス, ...]} （重複したものだけ）
    """
    by_path = {}
    for asset in targets:
        path = build_output_path(asset, output_dir, output_extension(asset, extension))
        by_path.setdefault(path.lower(), []).append(asset.get_path_name())

    return {path: names for path, names in by_path.items() if len(names) > 1}


# ==============================================================================
#  エクスポートオプション
# ==============================================================================

def make_export_options():
    """
    glTF エクスポートオプションを生成する。

    注意:
      export_uniform_scale はデフォルト 0.01（cm -> m 変換）。
      Unreal は cm 単位、glTF は m 単位が標準のため通常はこのままで良い。
      UE のスケール感のまま出したい場合は 1.0 を設定する。
    """
    options = unreal.GLTFExportOptions()

    # プロパティ名がバージョンで変わっても全体が止まらないよう try で保護する
    try:
        options.export_preview_mesh = EXPORT_PREVIEW_MESH
    except Exception as e:
        log_warn("export_preview_mesh の設定に失敗: {}".format(e))

    # 必要に応じてスケールを上書きしたい場合は以下を有効化:
    # options.export_uniform_scale = 1.0

    return options


# ==============================================================================
#  単一アセットのエクスポート
# ==============================================================================

def _extract_messages(messages):
    """
    export_to_gltf が返すメッセージ構造体から errors / warnings を
    可能な範囲で文字列化する（プロパティ名差異は握りつぶす）。
    """
    if messages is None:
        return ""

    parts = []
    for prop in ("errors", "warnings"):
        try:
            values = messages.get_editor_property(prop)
            if values:
                parts.extend(str(v) for v in values)
        except Exception:
            pass
    return " | ".join(parts)


def export_single_asset(asset, output_path, options):
    """
    1 アセットをエクスポートする。
    戻り値: (成功フラグ bool, メッセージ str)

    例外はここでは捕捉せず、呼び出し側のループでまとめて処理する。
    """
    # 出力先サブフォルダを作成（既存でもエラーにしない）
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    # アセット出力では selected_actors は空集合を渡す
    # （selected_actors は UWorld 出力時のみ有効）
    selected_actors = set()

    # 同名ファイルが存在する場合は exporter が自動上書きする
    result = unreal.GLTFExporter.export_to_gltf(
        asset, output_path, options, selected_actors
    )

    # 戻り値の形がバインディングにより
    #   - bool 単体
    #   - (bool, messages) のタプル（OutMessages 出力パラメータ）
    # のどちらでも動くよう両対応する。
    if isinstance(result, tuple):
        success = bool(result[0])
        messages = result[1] if len(result) > 1 else None
    else:
        success = bool(result)
        messages = None

    return success, _extract_messages(messages)


def export_sound_wave(asset, output_path):
    """
    SoundWave を .wav で書き出す。
    戻り値: (成功フラグ bool, メッセージ str)

    glTF は 3D モデルとアニメーションの形式で音声を持てないため、
    UE 標準の WAV エクスポーター（SoundExporterWAV）を使う。
    書き出されるのはインポートした元の PCM データ。
    """
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = output_path
    task.exporter = unreal.SoundExporterWAV()
    task.automated = True          # 上書き確認などのダイアログを出さない
    task.prompt = False
    task.replace_identical = True  # 同名ファイルは上書きする（glTF 側と同じ扱い）

    success = bool(unreal.Exporter.run_asset_export_task(task))

    # 失敗理由はタスクの errors に入る（プロパティが無い版でも止めない）
    message = ""
    try:
        errors = task.get_editor_property("errors")
        if errors:
            message = " | ".join(str(e) for e in errors)
    except Exception:
        pass

    return success, message


# ==============================================================================
#  出力ファイルの検査
#    エクスポータの戻り値は「書けたか」しか教えてくれない。
#    中身が壊れていても Success になるため、出したファイルを開いて報告する。
#    特に「キーが2本に潰れている」は再生しても静止するだけで気付けない。
# ==============================================================================

def read_gltf_json(path):
    """
    .gltf / .glb のどちらからでも JSON 部分を読んで dict で返す。
    読めなければ None。

    .glb は先頭12バイトがヘッダ、続く8バイトが「JSONチャンク長 + 型」、
    その後に JSON 本体が入っている。
    """
    try:
        if path.lower().endswith(".glb"):
            with open(path, "rb") as f:
                if len(f.read(12)) < 12:
                    return None
                chunk_header = f.read(8)
                if len(chunk_header) < 8:
                    return None
                chunk_length = struct.unpack("<II", chunk_header)[0]
                return json.loads(f.read(chunk_length).decode("utf-8"))

        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)

    except Exception as e:
        log_warn("出力ファイルを読めませんでした（検査のみ失敗）: {} : {}".format(path, e))
        return None


def load_reference_bone_names(reference_model_path):
    """
    照合用モデルのボーン名を集合で返す。指定が無い / 読めない場合は None。

    エンジンはアニメーションのチャンネルを「ボーン名」でモデルへ結び付けるため、
    ここが食い違うとそのボーンだけ無言で動かない。出した直後に気付けるようにする。
    """
    if not reference_model_path:
        return None

    if not os.path.isfile(reference_model_path):
        log_warn("照合用モデルが見つかりません（照合をスキップ）: {}".format(reference_model_path))
        return None

    doc = read_gltf_json(reference_model_path)
    if doc is None:
        return None

    names = set()
    for node in doc.get("nodes", []):
        if node.get("name"):
            names.add(node["name"])

    log_info("照合用モデル: {} （ボーン {} 本）".format(
        os.path.basename(reference_model_path), len(names)))
    return names


def inspect_exported_file(output_path, reference_bone_names):
    """
    出力ファイルを開いて内容を Output Log へ報告する。
    問題を見つけても警告を出すだけで、エクスポート自体の成否は変えない。
    """
    doc = read_gltf_json(output_path)
    if doc is None:
        return

    nodes = doc.get("nodes", [])
    accessors = doc.get("accessors", [])
    animations = doc.get("animations", [])
    named_nodes = [n["name"] for n in nodes if n.get("name")]

    log_info("    ノード {} 個（名前あり {} 本）".format(len(nodes), len(named_nodes)))

    # --- .gltf は .bin が別ファイル。片方だけ持ち出すと壊れるので存在を確認する ---
    for buffer in doc.get("buffers", []):
        uri = buffer.get("uri")
        if not uri or uri.startswith("data:"):
            continue
        if not os.path.isfile(os.path.join(os.path.dirname(output_path), uri)):
            log_error("    参照している {} が出力されていません".format(uri))

    # --- アニメーションごとの内訳 ---
    for animation in animations:
        samplers = animation.get("samplers", [])
        channels = animation.get("channels", [])

        key_counts = Counter()
        for channel in channels:
            try:
                input_index = samplers[channel["sampler"]]["input"]
                key_counts[accessors[input_index].get("count", 0)] += 1
            except Exception:
                continue

        breakdown = ", ".join(
            "{}キー×{}本".format(count, num) for count, num in sorted(key_counts.items()))

        # エンジンが照合に使うのはファイル名ではなくこの名前。
        # Sequencer の targetAnimName にはこれをそのまま書く。
        log_info("    アニメ名 {}  チャンネル {} 本  [{}]".format(
            animation.get("name", ""), len(channels), breakdown))

        collapsed = sum(num for count, num in key_counts.items()
                        if count <= COLLAPSED_KEY_THRESHOLD)
        if collapsed > 0 and len(key_counts) > 1:
            log_warn("    キーが {} 本以下に潰れたチャンネルが {} 本あります"
                     "（そのボーンは再生しても動きません）".format(
                         COLLAPSED_KEY_THRESHOLD, collapsed))

    if not animations:
        log_info("    アニメーションは含まれていません")

    # --- 照合用モデルとのボーン名突き合わせ ---
    if reference_bone_names:
        unmatched = [name for name in named_nodes if name not in reference_bone_names]
        matched_count = len(named_nodes) - len(unmatched)

        if matched_count == 0:
            log_error("    ボーン名がモデルと一つも一致しません（別の骨格の可能性）")
        elif unmatched:
            log_warn("    モデルに無いボーンが {} / {} 本あります（例 {}）".format(
                len(unmatched), len(named_nodes), unmatched[0]))
        else:
            log_info("    ボーン名はモデルと完全に一致しています（{} 本）".format(matched_count))


# ==============================================================================
#  結果ダイアログ（任意・失敗しても処理に影響しない）
# ==============================================================================

def show_result_dialog(success_count, failed_count, skipped_count, cancelled):
    """結果をエディタのメッセージダイアログで表示する（失敗時はログのみ）。"""
    title = "glTF Export"
    status = "Cancelled" if cancelled else "Finished"
    body = (
        "{status}\n\n"
        "Success : {ok}\n"
        "Failed  : {ng}\n"
        "Skipped : {skip}（対応外アセット）\n\n"
        "詳細は Output Log を確認してください。"
    ).format(status=status, ok=success_count, ng=failed_count, skip=skipped_count)

    try:
        unreal.EditorDialog.show_message(title, body, unreal.AppMsgType.OK)
    except Exception as e:
        # ダイアログ API が異なっても致命的ではないのでログだけ残す
        log_warn("結果ダイアログ表示に失敗（ログのみ）: {}".format(e))


# ==============================================================================
#  メイン処理
# ==============================================================================

def run():
    """ツールのエントリポイント。"""
    log_info("Export Start")

    # --- 1. 選択アセット取得 ---
    selected = unreal.EditorUtilityLibrary.get_selected_assets()
    if not selected:
        log_warn("アセットが選択されていません。Content Browser で選択してください。")
        show_result_dialog(0, 0, 0, cancelled=False)
        return

    # --- 2. 対応アセットのみ抽出 ---
    targets = [a for a in selected if is_supported_asset(a)]
    skipped_count = len(selected) - len(targets)

    if skipped_count > 0:
        log_warn("対応外のため {} 件スキップしました。".format(skipped_count))

    # SoundCue は複数の音を組み合わせるグラフで、音声ファイルそのものではないので出せない。
    # 音を選んだつもりで黙ってスキップされると分からないので、名前を出して知らせる。
    sound_cues = [a.get_name() for a in selected if isinstance(a, unreal.SoundCue)]
    if sound_cues:
        log_warn("SoundCue は .wav にできないためスキップしました（中の SoundWave を選んでください）: {}".format(
            ", ".join(sound_cues)))

    if not targets:
        log_warn("エクスポート対象（StaticMesh/SkeletalMesh/AnimSequence/SoundWave）がありません。")
        show_result_dialog(0, 0, skipped_count, cancelled=False)
        return

    # --- 3. 出力先を選ばせる ---
    #   選ばれなければ何もしない。勝手にどこかへ出力はしない。
    output_dir = ask_output_dir()
    if not output_dir:
        show_result_dialog(0, 0, skipped_count, cancelled=True)
        return

    extension = ".glb" if EXPORT_GLB else ".gltf"
    options = make_export_options()

    log_info("出力先    : {}".format(output_dir))
    log_info("出力形式  : {}（音声は .wav） {}".format(
        extension, "（平置き）" if FLATTEN_OUTPUT else "（UEの構造を再現）"))
    log_info("対象件数  : {}".format(len(targets)))

    # --- 4. 出力パスの衝突を先に潰す ---
    #   平置きだと UE 内の別フォルダにある同名アセットが同じファイルへ書かれ、
    #   片方が黙って消える。始めてしまう前に止める。
    collisions = find_output_collisions(targets, output_dir, extension)
    if collisions:
        log_error("出力ファイル名が衝突しています。中断しました。")
        for path, names in collisions.items():
            log_error("  {} <- {}".format(os.path.basename(path), ", ".join(names)))
        log_error("UE 側でアセット名を変えるか、FLATTEN_OUTPUT を False にしてください。")
        show_result_dialog(0, len(collisions), skipped_count, cancelled=False)
        return

    reference_bone_names = load_reference_bone_names(resolve_reference_model(output_dir))

    success_count = 0
    failed_count = 0
    failed_names = []
    cancelled = False

    # --- 5. 進捗バー付きで一括エクスポート ---
    #   total_work = 対象件数。各ループで enter_progress_frame(1, 名前) して 1 進める。
    total_work = len(targets)
    with unreal.ScopedSlowTask(total_work, "Exporting...") as task:
        # 第1引数 True で Cancel ボタン付きダイアログを表示する
        task.make_dialog(True)

        for asset in targets:
            # キャンセル要求があればループを抜ける（途中までの結果は保持）
            if task.should_cancel():
                cancelled = True
                log_warn("ユーザー操作によりキャンセルされました。")
                break

            asset_name = asset.get_name()

            # 進捗を1つ進め、現在処理中のアセット名をダイアログへ表示
            task.enter_progress_frame(1, asset_name)

            output_path = build_output_path(asset, output_dir, output_extension(asset, extension))

            # --- エラー処理: 1件失敗しても止めず最後まで続行する ---
            try:
                if is_sound(asset):
                    ok, messages = export_sound_wave(asset, output_path)
                else:
                    ok, messages = export_single_asset(asset, output_path, options)
                if ok:
                    success_count += 1
                    log_info("Export {}  ->  {}".format(asset_name, output_path))
                    # 検査は glTF の中身（ボーン・アニメ）を見るものなので、音声では行わない
                    if INSPECT_AFTER_EXPORT and not is_sound(asset):
                        inspect_exported_file(output_path, reference_bone_names)
                else:
                    failed_count += 1
                    failed_names.append(asset_name)
                    detail = " : {}".format(messages) if messages else ""
                    log_error("Failed {}（exporter が False を返却）{}".format(asset_name, detail))

            except Exception as e:
                # 想定外の例外も握りつぶしてカウントだけして継続
                failed_count += 1
                failed_names.append(asset_name)
                log_error("Failed {} : {}".format(asset_name, e))

    # --- 6. 結果サマリ ---
    log_info("Finished")
    log_info("Success : {}".format(success_count))
    log_info("Failed  : {}".format(failed_count))
    if failed_names:
        log_warn("失敗一覧: {}".format(", ".join(failed_names)))

    show_result_dialog(success_count, failed_count, skipped_count, cancelled)


# ==============================================================================
#  実行
# ==============================================================================
# スクリプトとして実行された時点で run() を呼ぶ（「実行するだけ」で使える）。
run()
