#pragma once
#include "StateTreeData.h"
#include <memory>
#include <vector>
#include <string>
#include <DirectXMath.h>

// AIの判断材料を格納するブラックボード（Blackboard）
struct AIBlackboard {
    //// --- 座標・空間データ ---
    //DirectX::SimpleMath::Vector3 targetPosition; // ターゲットの座標
    //float distanceToTarget = 0.0f;               // ターゲットとの距離

    //// --- ステータス・フェーズデータ ---
    //float healthPercentage = 100.0f;             // 自分の残りHP
    //int currentPhase = 1;                        // ボスの現在のフェーズ（1=前半戦, 2=後半戦）

    //// --- ダメージ蓄積・回避システム ---
    //float accumulatedDamage = 0.0f;              // 現在溜まっているダメージ量
    //float evadeDamageThreshold = 20.0f;          // いくつダメージを受けたら逃げるかの閾値
};


// AIの動的な状態（Entityごとに保持）
struct StateTreeComponent {
    std::shared_ptr<StateTreeAsset> sharedAsset;
    std::string assetPath = "Assets/StateTree/BossAI.json";
    std::string loadedAssetPath = "";

    StateID currentStateId = 0; // 現在滞在しているState

    // タスク用のタイマーや実行状態の記憶領域（動的確保）
    std::vector<float> taskTimers;

    AIBlackboard blackboard;
};