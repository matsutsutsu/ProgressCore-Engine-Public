#pragma once
#include <vector>
#include <cstdint>
#include "AnimSequence.h"

// ===================================================================================
// ファイル: AnimStateMachine.h
// 概要: アニメーションの「ルールブック」と「状態記憶」のデータ構造
// 
// [ 役割 ]
// 1. マスターデータ (AnimStateGraph等): 
//    「どの条件を満たせば、どのアニメへ遷移するか」のノードネットワークを定義。
//    JSONからロードされ、全エンティティで共有される。
// 2. コンポーネント (AnimStateMachineComponent):
//    各エンティティが「現在どのステート(ノード)にいるか」を記憶する器。
// ===================================================================================

// 条件演算子
enum class AnimConditionOp { Equal, Greater, Less };

// 遷移条件 (例: Speed > 0.1)
struct AnimCondition {
    uint32_t paramHash;
    AnimConditionOp op;
    float threshold; // Boolの場合は 1.0f(true) / 0.0f(false) として扱う
};

// 遷移の矢印
struct AnimTransition {
    uint32_t targetStateHash;              // 遷移先のステートID
    std::vector<AnimCondition> conditions; // 全て満たせば遷移する (AND条件)
};

// ステート (ノード)
struct AnimState {
    uint32_t stateHash;
    const AnimSequence* sequence = nullptr;  // 再生するアニメーションデータ
    std::vector<AnimTransition> transitions; // ここから伸びる矢印のリスト
};

// グラフ全体 (マスターデータとして使い回す)
struct AnimStateGraph {
    std::vector<AnimState> states;
    uint32_t entryStateHash = 0; // 初期ステート
};

// Entityに持たせるコンポーネント (現在の状態を記憶する)
struct AnimStateMachineComponent {
    const AnimStateGraph* graph = nullptr; // 共有データへのポインタ
    uint32_t currentStateHash = 0;         // 現在自分がいるステート
};
