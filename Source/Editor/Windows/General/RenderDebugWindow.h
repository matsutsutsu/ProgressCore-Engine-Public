#pragma once
// ===========================================================================
// 概要: 描画パスが RenderDebugRegistry に登録した中間テクスチャを一覧表示するウィンドウ
//
// 責務:
//   - GBuffer・深度・法線・シャドウなどの中間バッファをサムネイルで表示する
//   - 種別に応じて RenderTargetVisualizer で表示用の色に変換する
//   - 露出・チャンネル・配列スライスなどの表示設定を持つ
//
// 備考: 描画パスとは RenderDebugRegistry を介してだけつながる。既定は非表示で、
//       Window メニューから開く
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Editor/Windows/General/RenderTargetVisualizer.h"

class RenderDebugWindow : public EditorWindow
{
public:
    // 既定は非表示（必要時に Window メニューから開く）
    RenderDebugWindow() : EditorWindow("Render Debug", false) {} // 既定は非表示

protected:
    void DrawContents(EditorContext &context) override;

private:
    RenderTargetVisualizer m_viz;

    // 表示設定（全エントリ共通）
    float m_thumbWidth    = 320.0f; // サムネイルの表示幅（px）
    float m_exposure      = 1.0f;   // HDR 表示の露出
    int   m_channelSelect = 0;      // 0=RGB 1=R 2=G 3=B 4=A
    int   m_arraySlice    = 0;      // 配列テクスチャの表示スライス
    float m_nearZ         = 0.1f;    // 深度の線形化に使う near。カメラがあれば毎フレーム取り直す
    float m_farZ          = 1000.0f; // 同じく far
};
