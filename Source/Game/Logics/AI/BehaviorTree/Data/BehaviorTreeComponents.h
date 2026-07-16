#pragma once
#include <cstdint>
#include <memory>
#include "SimpleMath.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

#include "Engine/Serialization/Core/EntityLink.h"

/**
 * @file BehaviorTreeComponents.h
 * @brief ECSエンティティが保持するAIの状態管理コンポーネント
 * * 役割：
 * 各エンティティ固有の「現在の思考位置」や「判断材料（Blackboard）」を管理する。
 * * 設計思想：
 * 「思考（Data）」と「状態（Component）」を分離。
 * コンポーネントには実行時のみ必要な最小限の状態のみを持たせ、
 * 巨大な木構造自体はアセットとして全Entityで共有する（メモリ節約）。
 */

 // AIの判断材料を格納するブラックボード（Blackboard）
struct AIBlackboard {
    // --- 座標・空間データ ---
    DirectX::SimpleMath::Vector3 targetPosition; // ターゲットの座標
    float distanceToTarget = 0.0f;               // ターゲットとの距離

    // --- ステータス・フェーズデータ ---
    float healthPercentage = 100.0f;             // 自分の残りHP
    int currentPhase = 1;                        // ボスの現在のフェーズ（1=前半戦, 2=後半戦）

    // --- ダメージ蓄積・回避システム ---
    float accumulatedDamage = 0.0f;              // 現在溜まっているダメージ量
    float evadeDamageThreshold = 20.0f;          // いくつダメージを受けたら逃げるかの閾値
};

// AIの内部状態とブラックボード（直列化対象）
// 例えば、どのノードを実行中か、どのアセットを使うかなどの情報を持つ
struct BehaviorTreeComponent {
    // --- 巨大なオブジェクト群 (ポインタ/コンテナ系) ---
    std::shared_ptr<BTAsset> sharedAsset;

    std::string assetPath = "Data/BehaviorTree/BossAI.json";
    std::string loadedAssetPath = "";
    std::string phase2AssetPath = "Data/BehaviorTree/BossAI_Phase2.json";
    std::string lastFrameTreeLog = "";

    std::vector<BTDebugState> debugNodeStates;
    std::vector<float> nodeTimers;
    std::vector<int> runningNodes;

    // --- 構造体 ---
    AIBlackboard blackboard;

    // --- プリミティブ型 (4バイト) ---
    AssetID assetId = 0;
    float phase2HealthThreshold = 50.0f;

    // --- プリミティブ型 (2バイト) ---
    BTNodeID runningNodeId = 0;
    BTNodeID previousRunningNodeId = 0xFFFF;
    ActionID lastLogActionId = 0xFFFF;
};



// AIシステムからの「出力」を受け取るコンポーネント（毎フレームリセットされる使い捨てデータ）
// 例えば、AIが「近接攻撃してほしい」「ここに移動してほしい」といった命令をこのコンポーネントに書き込む
struct BossCommandComponent {
    CCL::Serialization::EntityLink targetPlayerId; // 誰を狙うか

    DroneFormationType requestFormation = DroneFormationType::Hidden;

    // AIが現在「筋肉にやってほしい」と要求しているActionIDをそのまま渡す
    ActionID currentActionId = 0;

    /// @brief ドローン陣形攻撃の「今撃て」信号（実行時専用・非シリアライズ）。
    ///        BTの DroneStrikeAction が true、DroneAction(陣形指示) が false にする。
    ///        DroneCommandSystem が各ドローンの wantsAttack へ配布する。
    bool droneAttackActive = false;
};