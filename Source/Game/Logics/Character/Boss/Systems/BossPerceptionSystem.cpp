/// @file  BossPerceptionSystem.cpp
/// @brief ボスの状況認識の実装。BattleContext（世界の事実）を読んで
///        blackboard（各ボスの主観的認識）を更新する。
///
/// BattleContext.playerPos を使うことで、「プレイヤーをViewで探す」処理を
/// 各システムで重複させない（1対1なので探索はBattleContext更新側で1回のみ）。
#include "BossPerceptionSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <SimpleMath.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void BossPerceptionSystem::Update(float /*rawDt*/)
{
    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return; // プレイヤーがまだいなければ何もしない

    ForEach([&](BehaviorTreeComponent    &btComp,
                const TransformComponent &trans,
                const HealthComponent    &health,
                const TimeStateComponent & /*time*/) {
        // --- HP割合の同期 ---
        if (health.maxHealth > 0.0f)
        {
            btComp.blackboard.healthPercentage =
                (health.currentHealth / health.maxHealth) * 100.0f;
        }

        // --- ターゲットの決定 ---
        //   1対1なので今は常にプレイヤー。ここが「誰を狙うか」を決める唯一の場所で、
        //   雑魚を群れで出すときは、この1行を「一番近い/一番脅威の相手を選ぶ」に
        //   差し替えるだけで済むようにしてある。
        //   以前は狙う相手という概念がコード上に無く、BattleContext.playerPos を
        //   全AIへ直接配っていた（敵が1体しかいないから成立していた）。
        btComp.blackboard.targetEntity = battle.playerEntity;

        // --- ターゲットの座標・距離の同期 ---
        //   プレイヤーの座標は BattleContext が毎フレーム集計済みなので、そこから取る
        //   （全AIが同じ Transform を引き直さないための一元化）。
        //   プレイヤー以外を狙うようになったら、そのときだけ Transform を引く。
        Vector3 targetPos(battle.playerPos);
        if (btComp.blackboard.targetEntity != battle.playerEntity)
        {
            const auto *targetTrans =
                m_world->GetComponent<TransformComponent>(btComp.blackboard.targetEntity);
            if (!targetTrans) return; // ターゲットが消えている。今フレームは更新しない
            targetPos = Vector3(targetTrans->position);
        }

        btComp.blackboard.targetPosition   = targetPos;
        btComp.blackboard.distanceToTarget = Vector3::Distance(Vector3(trans.position), targetPos);
    });
}

// L02_PreUpdate：BehaviorTreeSystem(L02_Update) が blackboard を読む前に認識を確定させる。
//   同ステージの BossPhaseSystem / DroneBrainSystem とは互いに依存しない
//   （あちらは blackboard を読まない）ので、順序が決まらなくても問題ない。
REGISTER_LOGIC_SYSTEM(BossPerceptionSystem, Priority::LogicStage::L02_PreUpdate);