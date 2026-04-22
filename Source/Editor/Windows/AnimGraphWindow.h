// ===================================================================================
// ファイル: AnimGraphWindow.h
// 概要: アニメーション・ステートマシンを視覚的に編集するノードエディタ
// 
// [ 役割 ]
// AnimGraphSerializer を用いて JSON から AnimStateGraph をロードし、
// imnodes ライブラリを用いてノード（State）とリンク（Transition）を描画する。
// デザイナーが C++ や JSON を直接触ることなく、GUI上で遷移ルールを構築するためのツール。
// ===================================================================================
#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Engine/GamePlay/Animation/AnimStateMachine.h" // リファクタリング後のパスに合わせてください
#include <string>

// 前方宣言
struct ImNodesContext;

class AnimGraphWindow : public EditorWindow {
public:
    AnimGraphWindow();
    virtual ~AnimGraphWindow();

    // エディタ起動・終了時のフック
    // virtual void Initialize() override; // EditorWindowの仕様にあれば
    // virtual void Shutdown() override;

protected:
    void DrawContents(EditorContext& context) override;

private:
    void DrawToolbar();
    void DrawNodeWorkspace();
    void DrawInspector();

    // ヘルパー関数
    void LoadGraph(const std::string& path);
    void SaveGraph(const std::string& path);

private:
    // メモリ上のマスターデータ（編集中）
    AnimStateGraph _currentGraph;
    std::string _currentFilePath = "Assets/Animations/PlayerAnimGraph.json";

    ImNodesContext* _nodeContext = nullptr;

    // 選択状態の管理
    int _selectedNodeId = -1;
    int _selectedLinkId = -1;

    // UI用のダミー変数（新規ステート追加用）
    char _newStateName[64] = "";
};