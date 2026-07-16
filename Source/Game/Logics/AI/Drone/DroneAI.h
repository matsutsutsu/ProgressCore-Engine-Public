/// @file  DroneAI.h
/// @brief ドローン個体の StateTree（企画書 第1段：判断層）。
///
/// 役割分担（CommanderAI と同じパターン）:
///   判断層（このファイル）  … 「今このドローンは何をすべきか」を決める
///   実行層（DroneMovementSystem・既存）… DroneComponent.currentState を見て物理的に動く
///
/// StateTree は DroneComponent.currentState を直接書き換えるだけ。
/// 「どう動くか」の実装（Lerp/Slerp等）には一切関与しない。
///
/// 状態（企画書の5状態のうち、今回実装する3状態）:
///   MoveToSlot（移動）: 目標位置から一定距離以上離れている → currentState = MoveToTarget
///   Active（攻撃実行） : 攻撃命令を受けている            → currentState = FireCharge
///   Forming（陣形維持・フォールバック）: それ以外         → currentState = Idle
///
///   ※ Forming と Idle は企画書では別概念（Forming=到着後の維持）だが、
///     DroneState::Idle の「有機的追従」が丁度その役割を担うため、
///     StateTree側では1状態(Forming)→1DroneState(Idle)の対応とする。
///   ※ Return（帰投）は今回未実装（企画書の第1段の中でも後回しにする対象）。
#pragma once
#include "Game/Logics/AI/StateTree/Core/StateTreeAsset.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeNode.h"
#include "Game/Logics/AI/StateTree/Core/NodeRegistry.h"
#include "Game/Logics/AI/StateTree/Core/NodeParamField.h"

#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/AI/Battle/BattleContext.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Platform/Core/Logger.h"

#include <SimpleMath.h>


namespace DroneAI
{
    // ====================================================================
    // タスク：DroneComponent.currentState を書き換える（ボスのSetActiveBTと同じ形）
    // ====================================================================
    struct SetDroneStateTask : public CCL::AI::ITask
    {
        struct Params
        {
            int targetState = 0;   // DroneState を int で保持（NodeParamFieldがenum非対応のため）
        };

        static const std::vector<CCL::AI::NodeParamField>& ParamFields()
        {
            static const std::vector<CCL::AI::NodeParamField> f = {
                NODE_PARAM_INT(Params, targetState, "Target DroneState", 0, 0, 2),
            };
            return f;
        }

        CCL::AI::RunStatus Tick(CCL::AI::ExecContext& ctx) override
        {
            const auto* p = static_cast<const Params*>(ctx.params);
            auto* drone = ctx.world->GetComponent<DroneComponent>(ctx.entity);
            if (!drone) return CCL::AI::RunStatus::Failed;

            drone->currentState = static_cast<DroneState>(p->targetState);
            return CCL::AI::RunStatus::Running;   // 滞在中ずっと維持
        }
    };


    // ====================================================================
    // 条件：陣形スロットへの初回接近がまだ必要か（MoveToSlotへ入る条件）
    // ====================================================================
    /// @note 毎フレーム動く targetPosition（円陣回転）との距離だけで判定すると、
    ///       Idle の追従で 1m 以内に収まり MoveToTarget が 1 フレームだけ点滅する。
    ///       初回到着を hasReachedSlot でラッチし、「接近フェーズ」と「維持フェーズ」を分離する。
    struct NotAtTargetCondition : public CCL::AI::ICondition
    {
        struct Params
        {
            float threshold = 2.5f;   // この距離以内で初回到着とみなす
        };

        static const std::vector<CCL::AI::NodeParamField>& ParamFields()
        {
            static const std::vector<CCL::AI::NodeParamField> f = {
                NODE_PARAM_FLOAT(Params, threshold, "Arrival Threshold(m)", 2.5f, 0.5f, 15.0f),
            };
            return f;
        }

        bool Evaluate(CCL::AI::ExecContext& ctx) override
        {
            const auto* p = static_cast<const Params*>(ctx.params);
            auto* drone = ctx.world->GetComponent<DroneComponent>(ctx.entity);
            auto* trans = ctx.world->GetComponent<TransformComponent>(ctx.entity);
            if (!drone || !trans) return false;

            // 一度到着したら、回転スロットとの距離では MoveToSlot に戻さない
            if (drone->hasReachedSlot) return false;

            DirectX::SimpleMath::Vector3 cur(trans->position);
            DirectX::SimpleMath::Vector3 target(drone->targetPosition);
            float dist = DirectX::SimpleMath::Vector3::Distance(cur, target);

            if (dist <= p->threshold)
            {
                drone->hasReachedSlot = true;   // 初回到着をラッチ
                return false;
            }

            return true;   // まだスロットへ向かう必要あり
        }
    };


    // ====================================================================
    // 条件：攻撃を要求されているか（Activeへ入る条件）
    // ====================================================================
    /// @note ボスBT（DroneStrikeAction）が BossCommandComponent.droneAttackActive を立て、
    ///       DroneCommandSystem がそれを各ドローンの wantsAttack へ配布する。
    ///       このフラグが立っている間だけ Active（=FireCharge）に入る。
    struct WantsAttackCondition : public CCL::AI::ICondition
    {
        bool Evaluate(CCL::AI::ExecContext& ctx) override
        {
            const auto* drone = ctx.world->GetComponent<DroneComponent>(ctx.entity);
            return drone && drone->wantsAttack;
        }
    };


    // ====================================================================
    // タスク：突撃開始（Activeへ入った瞬間の初期化）
    // ====================================================================
    /// @note EnterState で「タメ時間(stateTimer)」と「突撃方向(fireDirection)」を仕込む。
    ///       fireDirection は陣形（GuillotineSlam 等）が予備動作フェーズ中に既にセット
    ///       しているのでそれを尊重し、未設定のときだけプレイヤー方向をフォールバックにする。
    ///       これにより FireCharge 突入時に方向がロックされ、DroneMovementSystem の
    ///       「タメ→リリース」がコミットした直進として成立する（ホーミングしない）。
    struct BeginChargeTask : public CCL::AI::ITask
    {
        void EnterState(CCL::AI::ExecContext& ctx) override
        {
            auto* drone = ctx.world->GetComponent<DroneComponent>(ctx.entity);
            if (!drone) return;

            // 攻撃開始時のガード：直進モードから始める。
            //   陣形自体が動く攻撃（縦割り等）は Formation 側が毎フレーム true に上書きする。
            //   これにより前回の攻撃の追従モードが直進系の攻撃へ漏れない。
            drone->chargeFollowsTarget = false;

            // タメ時間（ProcessFireCharge が stateTimer>0 を「予備動作」として扱う）
            //   ※追従モード（chargeFollowsTarget=true）では ProcessFireCharge がタメを無視する。
            drone->stateTimer = 0.4f;

            // 突撃方向：陣形が既にセット済みなら尊重、未設定ならプレイヤー方向
            if (drone->fireDirection.LengthSquared() < 0.001f)
            {
                DirectX::SimpleMath::Vector3 dir = -DirectX::SimpleMath::Vector3::UnitZ;
                if (ctx.world->HasResource<BattleContext>())
                {
                    auto& battle = ctx.world->GetResource<BattleContext>();
                    DirectX::SimpleMath::Vector3 toPlayer =
                        DirectX::SimpleMath::Vector3(battle.playerPos) - drone->targetPosition;
                    if (toPlayer.LengthSquared() > 0.001f) { toPlayer.Normalize(); dir = toPlayer; }
                }
                drone->fireDirection       = dir;
                drone->targetLookDirection = dir;
            }
        }

        CCL::AI::RunStatus Tick(CCL::AI::ExecContext& /*ctx*/) override
        {
            // 維持のみ。初期化は EnterState 側で済ませている。
            return CCL::AI::RunStatus::Running;
        }
    };


    // ====================================================================
    // 登録
    // ====================================================================
    struct DroneAIIDs
    {
        CCL::AI::NodeTypeID setState;
        CCL::AI::NodeTypeID notAtTarget;
        CCL::AI::NodeTypeID wantsAttack;
        CCL::AI::NodeTypeID beginCharge;
    };

    inline DroneAIIDs RegisterNodes()
    {
        auto& reg = CCL::AI::NodeRegistry::Instance();
        DroneAIIDs ids;
        ids.setState    = reg.RegisterTask<SetDroneStateTask>("SetDroneState", "Drone");
        ids.notAtTarget = reg.RegisterCondition<NotAtTargetCondition>("NotAtTarget", "Drone");
        ids.wantsAttack = reg.RegisterCondition<WantsAttackCondition>("WantsAttack", "Drone");
        ids.beginCharge = reg.RegisterTask<BeginChargeTask>("BeginCharge", "Drone");
        return ids;
    }


    // ====================================================================
    // アセット構築
    // ====================================================================
    /// 優先度（子の順）:
    ///   Active     : 攻撃命令あり            → DroneState::FireCharge(=2)
    ///   MoveToSlot : 目標未到着               → DroneState::MoveToTarget(=1)
    ///   Forming    : フォールバック（到着済み）→ DroneState::Idle(=0)
    inline CCL::AI::StateTreeAsset BuildDroneAsset(const DroneAIIDs& ids)
    {
        using namespace CCL::AI;
        StateTreeAsset asset;
        asset.name = "DroneAI";

        const uint16_t root = asset.AddState("Root");

        // --- Active（最優先）---
        {
            const uint16_t active = asset.AddState("Active", root);
            asset.AddEnterCondition(active, ids.wantsAttack, 0, 0);   // パラメータ無し

            // 入場時に突撃を初期化（タメ・方向ロック）。EnterState を持つので先に置く。
            asset.AddTask(active, ids.beginCharge, 0, 0, 0);

            SetDroneStateTask::Params tp{ static_cast<int>(DroneState::FireCharge) };
            asset.AddTask(active, ids.setState, asset.AddParams(tp), sizeof(tp), 0);
        }

        // --- MoveToSlot ---
        {
            const uint16_t moveToSlot = asset.AddState("MoveToSlot", root);

            NotAtTargetCondition::Params cp{ 2.5f };
            asset.AddEnterCondition(moveToSlot, ids.notAtTarget, asset.AddParams(cp), sizeof(cp));

            SetDroneStateTask::Params tp{ static_cast<int>(DroneState::MoveToTarget) };
            asset.AddTask(moveToSlot, ids.setState, asset.AddParams(tp), sizeof(tp), 0);
        }

        // --- Forming（フォールバック・入場条件なし）---
        {
            const uint16_t forming = asset.AddState("Forming", root);

            SetDroneStateTask::Params tp{ static_cast<int>(DroneState::Idle) };
            asset.AddTask(forming, ids.setState, asset.AddParams(tp), sizeof(tp), 0);
        }

        (void)root;
        return asset;
    }

} // namespace DroneAI
