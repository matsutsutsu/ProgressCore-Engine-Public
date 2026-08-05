/// @file  StateTreeSystem.h
/// @brief StateTree の評価・タスク実行を担う ECS システム。
///
/// 設計方針（デカップリング）:
///   Filter 条件を意思決定に必要な最小データだけに絞ることで、
///   敵・ボス・トラップなど StateTreeComponent を持つ相手を
///   種類を問わず同じシステムで処理できるようにしている。
///
/// 依存（スケジューラが順序解決）:
///   Write StateTreeComponent   … 状態管理
///   Read  TimeStateComponent   … ローカルタイムスケール（ヒットストップ対応）
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Game/Logics/AI/StateTree/Core/StateTreeComponent.h"
#include "Engine/Time/TimeState.h"

// 前方宣言
namespace CCL::AI { struct StateTreeAsset; }

class StateTreeSystem : public CCL::ECS::IfSystem<
    StateTreeSystem,
    CCL::ECS::Write<StateTreeComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    StateTreeSystem() : IfSystem("StateTreeSystem") {}

    void Initialize() override;
    void Update(float dt) override;

private:
    // 1 Entity の評価メイン
    void TickStateTree(const CCL::AI::StateTreeAsset& asset,
                       StateTreeComponent&            st,
                       CCL::ECS::EntityID             entity,
                       float                          dt);

    /// @brief ルートから葉まで選択し、結果を outPath/outDepth に格納する。
    bool SelectStates(const CCL::AI::StateTreeAsset& asset,
                      CCL::ECS::EntityID             entity,
                      uint16_t                       outPath[StateTreeComponent::MAX_DEPTH],
                      uint8_t&                       outDepth);

    /// @brief 1状態の入場条件を全評価。
    bool EvaluateEnterConditions(const CCL::AI::StateTreeAsset& asset,
                                 uint16_t                       stateIdx,
                                 CCL::ECS::EntityID             entity);

    /// @brief 1状態のタスクに EnterState を呼ぶ。
    void EnterState(const CCL::AI::StateTreeAsset& asset,
                    StateTreeComponent&            st,
                    uint16_t                       stateIdx,
                    CCL::ECS::EntityID             entity);

    /// @brief 1状態のタスクに ExitState を呼ぶ。
    void ExitState(const CCL::AI::StateTreeAsset& asset,
                   StateTreeComponent&            st,
                   uint16_t                       stateIdx,
                   CCL::ECS::EntityID             entity);

    /// @brief 1状態のタスクを Tick する。
    void TickTasks(const CCL::AI::StateTreeAsset& asset,
                   StateTreeComponent&            st,
                   uint16_t                       stateIdx,
                   CCL::ECS::EntityID             entity,
                   float                          dt);
};