#pragma once
// ===========================================================================
// 概要: ゲーム画面を ImGui のウィンドウ内に表示するウィンドウ。
//
// 責務:
//   - ゲーム画面のテクスチャを 16:9 で中央に描き、その表示矩形を GameViewportInfo に書く
//   - フォーカス状態を EditorContext.gameViewFocused に書く（マウス視点の可否に使う）
//   - 選択中のカメラ台車の軌道を描き、制御点をギズモで動かす
//   - 一時停止中のクリックでエンティティを選択する（レイと AABB の判定）
// ===========================================================================
#include "Editor/Core/EditorWindow.h"

class GameViewWindow : public EditorWindow
{
public:
    GameViewWindow() : EditorWindow("Game View") {}

protected:
    void DrawContents(EditorContext &context) override;
};