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
#include "Game/Logics/LevelGen/DungeonLayout.h"        // 手作りマップ

class DungeonGeneratorWindow : public EditorWindow
{
public:
    DungeonGeneratorWindow();

protected:
    void DrawContents(EditorContext& context) override;

private:
    // 生成モード（自動生成 / 手作り）
    enum class Mode { Procedural, HandMade };
    Mode _mode = Mode::Procedural;

    // UI 状態
    int   _seed = 12345;
    int   _floor = 1;
    bool  _hasResult = false;
    float _zoom = 8.0f;             // 1セルあたりのピクセル
    bool  _clearBeforeBuild = true; // 3D生成前に既存チャンクを消すか

    // ★ デバッグ調整用の生成パラメータ
    RuneForge::Dungeon::DungeonGenParams _gen;

    // 手作りマップ
    RuneForge::Dungeon::DungeonLayout _layout;
    char  _savePath[256] = "Assets/DebugMaps/test1.json";
    int   _editTool = 0;   // 0=壁/床, 1=スタート, 2=ポータル, 3=敵, 4=宝箱
    int   _resizeW = 21, _resizeH = 21;

    RuneForge::Dungeon::DungeonGenerateResult _lastResult;

    void Regenerate();
    void DrawGridCanvas();
    void DrawRoomList();

    // 現在のシード・層で 3D ダンジョンをシーンに構築する（context.world を使う）
    void BuildInScene(EditorContext& context);

    // 手作りモード用
    void DrawHandMadeControls(EditorContext& context);
    void DrawLayoutCanvas();   // クリックで壁/床/マーカーを編集
    void BuildLayoutInScene(EditorContext& context);
};