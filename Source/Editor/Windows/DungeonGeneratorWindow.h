#pragma once
//============================================================================
//  DungeonGeneratorWindow.h
//  RuneForge ダンジョン生成 — エディタツールウィンドウ（完全版）
//============================================================================
#include "Editor/Core/EditorWindow.h"
#include "Game/Logics/LevelGen/DungeonGenerator.h"     // 2Dプレビュー用
#include "Game/Logics/LevelGen/DungeonLevelBuilder.h"  // 3Dビルド用
#include "Game/Logics/LevelGen/DungeonLayout.h"        // 手作りマップ

class DungeonGeneratorWindow : public EditorWindow
{
public:
    DungeonGeneratorWindow();

protected:
    void DrawContents(EditorContext& context) override;

private:
    // コンテキストを伝播させて World にアクセスできるようにする
    void DrawProceduralTab(EditorContext& context);
    void DrawHandMadeTab(EditorContext& context);

    // キャンバス描画も分離（バグ防止のため）
    void DrawProceduralCanvas();
    void DrawHandMadeCanvas();

    void RegenerateProcedural();

private:
    // --- 共通・UI状態 ---
    int   _seed = 12345;
    int   _floor = 1;
    bool  _hasResult = false;
    float _zoom = 12.0f;             // 視認性向上のためデフォルトズーム拡大
    bool  _clearBeforeBuild = true;

    // --- 自動生成（Procedural）用データ ---
    RuneForge::Dungeon::DungeonGenParams _gen;
    RuneForge::Dungeon::DungeonGenerateResult _lastResult;

    // --- 手作りマップ（Hand-Made）用データ ---
    RuneForge::Dungeon::DungeonLayout _layout;
    char  _savePath[256] = "Assets/DebugMaps/test1.json";
    char  _currentEnemyPrefab[256] = "Assets/Prefabs/Enemy/Slime.json";
    int   _editTool = 0;            // 0=壁/床, 1=Start, 2=Portal, 3=Enemy, 4=Chest
    int   _resizeW = 21;
    int   _resizeH = 21;
};