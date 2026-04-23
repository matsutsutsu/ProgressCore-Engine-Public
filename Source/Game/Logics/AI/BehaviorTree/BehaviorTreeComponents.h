#pragma once
#include <cstdint>
#include "SimpleMath.h"
#include "Game/Logics/AI/BehaviorTree/BehaviorTreeData.h"

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
    DirectX::SimpleMath::Vector3 targetPosition; // ターゲットの座標
    float distanceToTarget = 0.0f;               // ターゲットとの距離
    float healthPercentage = 100.0f;             // 自分の残りHP
};

// AIの内部状態とブラックボード（直列化対象）
// 例えば、どのノードを実行中か、どのアセットを使うかなどの情報を持つ
struct BehaviorTreeComponent {
    CCL::AI::AssetID assetId = 0;        // どの知能（BTAsset）を使うか
    CCL::AI::BTNodeID runningNodeId = 0; // 現在実行中のノード

    // 判断材料をここに内包する（Blackboard）
    AIBlackboard blackboard;
};

// AIシステムからの「出力」を受け取るコンポーネント（毎フレームリセットされる使い捨てデータ）
// 例えば、AIが「近接攻撃してほしい」「ここに移動してほしい」といった命令をこのコンポーネントに書き込む
struct BossCommandComponent {
    bool requestMeleeAttack = false; // 近接攻撃をしてほしい！
    bool requestCharge = false;      // 突進してほしい！
    DirectX::SimpleMath::Vector3 moveTarget; // ここに移動してほしい！
};