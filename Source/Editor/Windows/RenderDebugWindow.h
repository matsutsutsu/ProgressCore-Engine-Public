/// @file  RenderDebugWindow.h
/// @brief RenderPass が RenderDebugRegistry に登録した中間テクスチャ(SRV)を一覧表示するデバッグウィンドウ。
///        役割     : GBuffer/深度/法線/シャドウ等の中間バッファを ImGui 上で視認する。
///        責務     : Registry の一覧を RenderTargetVisualizer で可視化し ImGui::Image で貼る（v2）。
///        位置付け : 既存 EditorWindow 群の一つ。Window メニューからトグルする。
///        設計方針 : パスとは疎結合（Registry 経由）。種別ごとに深度線形化/法線remap/AO/HDR/配列
///                   スライスを可視化シェーダーで正しく表示する。
#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Editor/Windows/RenderTargetVisualizer.h"

/// @brief 中間レンダーテクスチャのデバッグ一覧ウィンドウ。
class RenderDebugWindow : public EditorWindow
{
public:
    // 既定は非表示（必要時に Window メニューから開く）。
    RenderDebugWindow() : EditorWindow("Render Debug", false) {}

protected:
    void DrawContents(EditorContext& context) override;

private:
    RenderTargetVisualizer _viz;

    // 表示コントロール（全エントリ共通）
    float _thumbWidth    = 320.0f; // サムネイルの表示幅（px）
    float _exposure      = 1.0f;   // HDR 表示の露出
    int   _channelSelect = 0;      // 0=RGB 1=R 2=G 3=B 4=A
    int   _arraySlice    = 0;      // 配列テクスチャの表示スライス
    float _nearZ         = 0.1f;   // 深度線形化用（カメラから自動取得、無ければ手動）
    float _farZ          = 1000.0f;
};
