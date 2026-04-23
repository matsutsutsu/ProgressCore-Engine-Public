/**
 * @file BehaviorTreeComponents.h
 * @brief ビヘイビアツリーの状態と、AIからの出力コマンドを管理するコンポーネント群
 */
#pragma once
#include <cstdint>
#include "SimpleMath.h"
#include "Game/Logics/AI/BehaviorTree/BehaviorTreeData.h"

 // AIの内部状態とブラックボード（直列化対象）
struct BehaviorTreeComponent {
    CCL::AI::AssetID assetId = 0;
    CCL::AI::BTNodeID runningNodeId = 0; // 現在実行中のノード

    // Blackboardデータ（本来は分離も検討するが、キャッシュ効率のため統合）
    DirectX::SimpleMath::Vector3 targetPosition = DirectX::SimpleMath::Vector3::Zero;
    float distanceToTarget = 0.0f;
    float healthPercentage = 100.0f;
};

// AIシステムからの「出力」を受け取るコンポーネント（毎フレームリセットされる使い捨てデータ）
struct BossCommandComponent {
    bool requestMeleeAttack = false;
    bool requestCharge = false;
    bool requestGuard = false;
    DirectX::SimpleMath::Vector3 moveTarget = DirectX::SimpleMath::Vector3::Zero;
};