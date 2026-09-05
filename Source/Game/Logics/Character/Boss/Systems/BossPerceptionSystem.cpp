// ボスの状況認識の実装。BattleContext（世界の事実）を読んで
//        blackboard（各ボスの主観的認識）を更新する。
//
// BattleContext.playerPos を使うことで、「プレイヤーをViewで探す」処理を
// 各システムで重複させない（1対1なので探索はBattleContext更新側で1回のみ）。
#include "BossPerceptionSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/GameEvent/GameEvents.h" // DamageTakenEvent
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <SimpleMath.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void BossPerceptionSystem::Initialize()
{
    // 被弾量を blackboard へ積む。
    //   これまで accumulatedDamage は「ゼロに戻す人」と「読む人」だけがいて、
    //   増やす人が存在しなかった。そのため C_DamageThresholdExceeded は
    //   木に置いても永久に偽で、回避行動へ移る枝が一度も通らなかった。
    //   イベント購読にしているのは、Combat 側（DamageResolutionSystem）に
    //   AI のコンポーネントを知らせないため。
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &ev) {
        if (ev.isPlayer) return; // プレイヤーの被弾は敵AIの判断材料ではない

        if (auto *bt = m_world->GetComponent<BehaviorTreeComponent>(ev.targetID))
        {
            bt->blackboard.accumulatedDamage += ev.damageAmount;

            // 被弾でモーションは止めない。止めるのは「次の技を選ぶ」判断だけ。
            bt->blackboard.timeSinceLastHit = 0.0f;
        }
    });
}


void BossPerceptionSystem::Update(float /*rawDt*/)
{
    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return; // プレイヤーがまだいなければ何もしない

    ForEach([&](BehaviorTreeComponent    &btComp,
                const TransformComponent &trans,
                const HealthComponent    &health,
                const TimeStateComponent &time) {
        // --- 被弾からの経過 ---
        //   個体の時計で数える。ヒットストップ中は時間が止まるので、
        //   「止まっている間に様子見が明ける」ということにならない。
        btComp.blackboard.timeSinceLastHit += time.localDt;

        // --- HP割合の同期 ---
        if (health.maxHealth > 0.0f)
        {
            btComp.blackboard.healthPercentage =
                (health.currentHealth / health.maxHealth) * 100.0f;

            // 蓄積した被弾を最大HPに対する割合へ直す。
            //   積む側（上の DamageTakenEvent 購読）は素の数値しか知らないので、
            //   HealthComponent を引けるこの場所で割合に直す。条件側が HP を
            //   引き直さずに済み、「被弾量を読む人」は割合だけを見ればよくなる。
            btComp.blackboard.accumulatedDamageRatio =
                (btComp.blackboard.accumulatedDamage / health.maxHealth) * 100.0f;
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