#pragma once
// ===========================================================================
// 概要: NavMesh の生成（Bake）の設定を調整し、生成を実行するウィンドウ。
//
// 責務:
//   - NavMeshBuildSettings の値を編集する
//   - Bake ボタンで NavMeshBuilder::BuildNavMesh を呼ぶ
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Editor/Utils/NavMeshBuilder.h"

class NavMeshWindow : public EditorWindow
{
public:
    NavMeshWindow();

protected:
    void DrawContents(EditorContext &context) override;

private:
    NavMeshBuildSettings m_navSettings; // Bake の設定値
};