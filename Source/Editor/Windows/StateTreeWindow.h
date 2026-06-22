/// @file  StateTreeWindow.h
/// @brief StateTree をアウトライン形式で編集するエディタウィンドウ。
///
/// 機能:
///   - 状態ツリーの表示・選択・追加・削除
///   - 選択状態へのタスク/条件の追加・削除（NodeRegistry からパレット生成）
///   - paramsの編集（NodeParamField からウィジェット自動生成）
///   - JSON の Open / Save（FileDialog + FileSystem）
///   - "Apply to World"：コンパイルして実行中の StateTreeRegistry へ登録
///   - 選択 Entity の実行中アクティブ状態のデバッグ表示
#pragma once
#include "Editor/Core/EditorWindow.h"   // パスは環境に合わせて調整
#include "Game/Logics/AI/StateTree/Serialization/StateTreeDef.h"
#include <string>
#include <vector>

class StateTreeWindow : public EditorWindow
{
public:
    StateTreeWindow();

protected:
    void DrawContents(EditorContext& context) override;

private:
    // --- UI 部品 ---
    void DrawToolbar(EditorContext& context);
    void DrawTreePanel();
    void DrawInspectorPanel();
    void DrawDebugPanel(EditorContext& context);

    void DrawStateNode(CCL::AI::StateDef& state, std::vector<int> path);
    void DrawNodeList(const char* label,
                      std::vector<CCL::AI::NodeDef>& nodes,
                      bool isCondition);
    void DrawParams(CCL::AI::NodeDef& node, bool isCondition);

    // --- 操作 ---
    void NewTree();
    void OpenTree();
    void SaveTree(bool saveAs);
    void ApplyToWorld(EditorContext& context);

    // path から StateDef を解決（add/delete でポインタが無効化するため毎回解決）
    CCL::AI::StateDef* Resolve(const std::vector<int>& path);

    // --- 遅延構造操作（描画中の vector 破壊を避ける）---
    enum class DeferredOp { None, AddChild, DeleteState };
    DeferredOp        _deferredOp = DeferredOp::None;
    std::vector<int>  _deferredPath;

    // --- 状態 ---
    CCL::AI::StateTreeDef _def;
    std::string           _currentPath;     // 編集中ファイルの相対パス
    std::vector<int>      _selectedPath;     // 選択中の状態へのパス（子indexの列）
    bool                  _hasSelection = false;
};
