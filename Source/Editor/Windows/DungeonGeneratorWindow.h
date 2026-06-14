#pragma once
//============================================================================
//  DungeonGeneratorWindow.h
//  RuneForge ダンジョン生成 — エディタツールウィンドウ（仕様書 §8）
//
//  ※ 仕様書では「ToolWindowBase 継承などの枠は別ファイルで提供」とあったが、
//     本プロジェクトの実体は EditorWindow（DrawContents(EditorContext&)）。
//
//  登録方法（EditorWindowManager::Initialize 内）:
//      RegisterWindow<DungeonGeneratorWindow>();
//============================================================================
#include "Editor/Core/EditorWindow.h"
#include "Game/Logics/LevelGen/DungeonGenerator.h"     // 2Dプレビュー用
#include "Game/Logics/LevelGen/DungeonLevelBuilder.h"  // 3Dビルド用

class DungeonGeneratorWindow : public EditorWindow
{
public:
    DungeonGeneratorWindow();

protected:
    void DrawContents(EditorContext& context) override;

private:
    // UI 状態
    int   _seed = 12345;
    int   _floor = 1;
    bool  _hasResult = false;
    float _zoom = 8.0f;             // 1セルあたりのピクセル
    bool  _clearBeforeBuild = true; // 3D生成前に既存チャンクを消すか

    RuneForge::Dungeon::DungeonGenerateResult _lastResult;

    void Regenerate();
    void DrawGridCanvas();
    void DrawRoomList();

    // 現在のシード・層で 3D ダンジョンをシーンに構築する（context.world を使う）
    void BuildInScene(EditorContext& context);
};