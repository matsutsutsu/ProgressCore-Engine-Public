#pragma once
// ===========================================================================
// 概要: BehaviorTreeWindow_*.cpp だけが共有する定数と小さな汎用関数。
//
// 責務:
//   - コメント枠の配色、ノードの幅、自動整列の間隔などの定数
//   - ノードエディタの ID の変換、パスからファイル名を取り出す関数
//
// 備考: 状態を持たない値と純粋関数だけを置く。BT 固有の処理（色の決定・JSON 化など）は
//       BehaviorTreeWindow の private メンバにして、クラス宣言から辿れるようにしてある。
// ===========================================================================
#include <cstdint>
#include <string>
#include <imgui.h>
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace BTEditor
{
    // コメント枠の配色1組。色の違いは見出し帯で見せる（内側を濃くすると中のノードが読めなくなる）
    struct GroupPalette
    {
        const char *name;   // インスペクタに出す色名
        ImU32       header; // 見出し帯
        ImU32       border; // 枠線
        ImU32       bg;     // 内側の塗り
    };

    // コメント枠の色の候補。役割ごと（索敵／近接／遠距離…）に塗り分ける用
    inline constexpr GroupPalette kGroupPalette[] = {
        {"グレー", IM_COL32(78, 78, 84, 235), IM_COL32(150, 150, 158, 160), IM_COL32(32, 32, 36, 110)},
        {"青", IM_COL32(42, 84, 140, 235), IM_COL32(90, 160, 240, 190), IM_COL32(22, 34, 52, 110)},
        {"緑", IM_COL32(40, 104, 60, 235), IM_COL32(96, 200, 120, 190), IM_COL32(22, 42, 28, 110)},
        {"黄", IM_COL32(126, 104, 36, 235), IM_COL32(230, 196, 90, 190), IM_COL32(46, 40, 20, 110)},
        {"赤", IM_COL32(128, 50, 44, 235), IM_COL32(232, 110, 100, 190), IM_COL32(48, 24, 22, 110)},
        {"紫", IM_COL32(98, 52, 122, 235), IM_COL32(196, 120, 226, 190), IM_COL32(40, 24, 48, 110)},
    };
    inline constexpr int kGroupPaletteCount = static_cast<int>(sizeof(kGroupPalette) / sizeof(kGroupPalette[0])); // 配色の数

    // 添字が範囲外（古いファイル・手編集）でもグレーに倒して落とさない
    inline const GroupPalette &GroupPaletteAt(int index)
    {
        return kGroupPalette[(index >= 0 && index < kGroupPaletteCount) ? index : 0];
    }

    // 通常ノードの幅。名前の長さに合わせて、この範囲で決める（短い名前のノードは細くなる）
    inline constexpr float kNodeMinWidth = 100.0f; // 下限。「Priority: 3」が収まる程度
    inline constexpr float kNodeMaxWidth = 180.0f; // 上限。これを超える名前は折り返す

    // 自動整列の間隔
    inline constexpr float kLayoutHGap     = 40.0f;  // 兄弟どうしの間隔
    inline constexpr float kLayoutVGap     = 90.0f;  // 段と段の間隔
    inline constexpr float kLayoutTreeGap  = 160.0f; // 根が複数あるときの木と木の間隔
    inline constexpr float kLayoutDefaultW = 200.0f; // まだ描かれておらずサイズ不明なノードの仮の幅
    inline constexpr float kLayoutDefaultH = 120.0f; // 同・仮の高さ

    inline constexpr float kGroupPad     = 24.0f; // コメント枠がノードを囲むときの余白
    inline constexpr float kGroupHeaderH = 30.0f; // コメント枠の見出し帯の高さ（＝掴める範囲）

    // ライブラリが返すID(uintptr_t)をこちらのID型へ戻す（こちらが uint32_t で発行したものなので切り詰めてよい）
    inline uint32_t ToEditorId(uintptr_t id) { return static_cast<uint32_t>(id); }

    // パスからファイル名を取り出す（"Assets/BehaviorTree/Common.json" → "Common.json"）
    inline std::string FileNameOf(const std::string &path)
    {
        // std::filesystem を通さないのは、C++20 の u8string() が std::u8string を返して std::string へそのまま繋げないため
        const size_t slash = path.find_last_of("/\\");
        return (slash == std::string::npos) ? path : path.substr(slash + 1);
    }
} // namespace BTEditor
