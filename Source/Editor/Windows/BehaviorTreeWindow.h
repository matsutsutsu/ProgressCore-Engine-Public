/**
 * @file BehaviorTreeWindow.h
 * @brief ビヘイビアツリーを視覚的に構築・編集するためのエディタウィンドウ
 * @note 【設計思想】
 * ランタイム（DOD向けフラット配列）とは完全に切り離された「エディタ専用のデータ構造」を持ちます。
 * 人間が編集しやすいグラフ構造（ノードとリンクの網目）を管理し、
 * 最終的にこれを機械が読みやすい配列（JSON）に変換（Bake）する役割を担います。
 */
#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include <string>
#include <vector>
#include <json.hpp>

namespace ax { namespace NodeEditor { struct EditorContext; } }

/**
 * @enum EditorBTNodeType
 * @brief エディタ上で視覚的に区別されるノードの種類
 */
enum class EditorBTNodeType {
    Root,      ///< 始点（1グラフに1つのみ）。入力ピンを持たない。
    Selector,  ///< 複合ノード：子を順番に実行し、1つでも成功すれば成功。
    Sequence,  ///< 複合ノード：子を順番に実行し、1つでも失敗すれば失敗。
    Condition, ///< 葉ノード：Blackboardの値を評価し、真偽を返す。出力ピンを持たない。
    Action,    ///< 葉ノード：実際の行動（攻撃、移動）を行う。出力ピンを持たない。
    Group,     ///< ★追加: コメント用グループノード（機能的な実行順序は持たない）
	Decorator  ///< ★追加: デコレータノード（子ノードの挙動を修飾する。例: Inverter, Cooldown, Retryなど）
};

/**
 * @struct BTPin
 * @brief ノード同士を接続するための「接続口」のデータ
 */
struct BTPin {
    uint32_t id;       ///< ピンのユニークID（ImGui Node Editorで必須）
    uint32_t nodeId;   ///< このピンが属しているノードのID
    bool isInput;      ///< true なら左側の入力ピン、false なら右側の出力ピン
};

/**
 * @struct BTLink
 * @brief ピンとピンを繋ぐ「線（リンク）」のデータ
 */
struct BTLink {
    int id;               ///< リンクのユニークID
    uint32_t startPinId;  ///< 接続元の出力ピンID
    uint32_t endPinId;    ///< 接続先の入力ピンID
};

/**
 * @struct EditorBTNode
 * @brief エディタ上で描画・操作される「付箋（ノード）」の実体
 */
struct EditorBTNode {
    uint32_t id;               ///< ノードのユニークID
    EditorBTNodeType type;     ///< ノードの種類
    std::string name;          ///< UI上に表示される名前

    // ランタイム用の設定値（インスペクタで編集する）
    ActionID actionOrConditionId = 0;

    // UI用の接続口
    BTPin inputPin;            ///< 親から繋がれる入力ピン
    BTPin outputPin;           ///< 子へ繋ぐための出力ピン（複数の子へ線を伸ばせる）

    // エディタ空間の座標
    float posX = 0.0f;
    float posY = 0.0f;

    // 配置フラグ。これが false の場合、次の描画フレームで座標を強制設定する
    bool positionInitialized = false;

    // 実行順序（Y軸ソート結果）。デバッグ表示用
    int executionOrder = -1;

    // グループノード用のサイズ（インスペクタで変更可能にする）
    float width = 300.0f;
    float height = 200.0f;

    float paramValue1 = 0.0f; // 例: 突進速度
    std::string targetKey = "Player"; // 例: Blackboardのキー

    // デコレーター設定
    int decoratorType = 0;
    float decoratorParam = 0.0f;

    // ランタイムとエディタを紐付ける魔法の変数
    // Bake時に「フラット配列の何番目になったか」を記憶しておく
    int runtimeFlatIndex = -1;
};

/**
 * @class BehaviorTreeWindow
 * @brief Node Editorライブラリをラップし、ウィンドウ描画とユーザー入力を処理するクラス
 */
class BehaviorTreeWindow : public EditorWindow {
public:
    BehaviorTreeWindow();
    virtual ~BehaviorTreeWindow();

    // Undo/Redo用の内部状態エクスポート/インポート
    nlohmann::json GetGraphStateAsJson();
    void RestoreGraphState(const nlohmann::json& state);

protected:
    virtual void DrawContents(EditorContext& context) override;

private:
    void DrawToolbar();
    void DrawNodeWorkspace(EditorContext& context);
    void DrawInspector();

    // 実際のゲーム開発に必須のUX機能
    void HandleContextMenu();
    void DeleteNodeAndLinks(uint32_t nodeId);

    // 実行順序を計算する関数の宣言
    void UpdateExecutionOrder();

    // 選択したノードと内部リンクを丸ごとコピーする関数
    void DuplicateSelectedNodes();

    ImVec2 _newNodeSpawnPos = { 0.0f, 0.0f }; ///< 右クリックした瞬間のスクリーン座標

    uint32_t GetNextId() { return ++_idCounter; }
    void CreateNode(EditorBTNodeType type, const std::string& name, float x, float y);
    void CompileAndSave(const std::string& path);

	void LoadGraph(const std::string& path);
	void SaveGraph(const std::string& path);

    // 変更をコマンド履歴に登録するヘルパー
    void RecordStateForUndo(const std::string& commandName);
    nlohmann::json _lastSavedState; // 直前の状態


private:
    ax::NodeEditor::EditorContext* _nodeContext = nullptr; ///< Node Editorの内部状態（キャンバスのズーム等）

    std::vector<EditorBTNode> _nodes; ///< 画面上の全付箋
    std::vector<BTLink> _links;       ///< 画面上の全接続線
    uint32_t _idCounter = 1;          ///< IDを被らせないためのカウンター

    std::string _currentFilePath = "Assets/AI/BossAI_Graph.json";
    bool _needInitialZoom = true;     ///< 初回起動時にカメラを全体に合わせるフラグ

    // 毎フレームのループを防ぐためのDirtyフラグ
    bool _executionOrderDirty = true;

    // CompileAndSave を破棄し、JSON構築用ヘルパー関数に変更
    nlohmann::json BuildBakedNodes();
};