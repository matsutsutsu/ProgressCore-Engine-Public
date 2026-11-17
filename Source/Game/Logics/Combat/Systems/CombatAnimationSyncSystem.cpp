#include "Game/Logics/Combat/Systems/CombatAnimationSyncSystem.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Physics/JoltIntegration/HitDiagnostics.h"

using namespace CCL::ECS;

namespace
{
    // Hitbox から親をたどり、最初に見つかった Animator を持つエンティティを返す（剣はキャラの子孫にある前提）
    //   SpawnHitboxSystem が作る球は親を持たないので InvalidEntityID になり、寿命まで有効な判定のまま
    EntityID ResolveAnimatorOwner(CCL::ECS::World *world, EntityID hitboxID)
    {
        EntityID cur = InvalidEntityID;
        if (const auto *t = world->GetComponent<TransformComponent>(hitboxID))
            cur = t->parent; // EntityLink から EntityID へ暗黙に変換される
        int guard = 0;
        while (cur != InvalidEntityID && cur != 0 && guard++ < 32)
        {
            if (world->GetComponent<AnimatorComponent>(cur))
                return cur;
            const auto *pt = world->GetComponent<TransformComponent>(cur);
            cur            = pt ? static_cast<EntityID>(pt->parent) : InvalidEntityID;
        }
        return InvalidEntityID;
    }
}



void CombatAnimationSyncSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 持ち主（Animator を持つ祖先）を探して覚える
    // 2. 持ち主の Animator を読む
    // 3. このステップで進んだ範囲に重なる HitBox 区間を探す
    // 4. 開いた・閉じたステップを見つけて書く

    // 書くのは自分の Hitbox だけで、持ち主の Animator は読むだけ（理由はヘッダーを参照）
    ForEachWithID([this](EntityID hitboxID, HitboxComponent &hb) {
        // ---------------------------------------------------------
        // 1. 持ち主
        // ---------------------------------------------------------

        // 見つからなかったことも覚える
        //   覚えないと、親を持たない判定（SpawnHitboxSystem の球・ドローン）が毎フレーム親をたどり直す
        if (!hb.ownerResolved)
        {
            hb.ownerEntity   = ResolveAnimatorOwner(m_world, hitboxID);
            hb.ownerResolved = true;
        }

        if (hb.ownerEntity == InvalidEntityID)
            return;

        // ---------------------------------------------------------
        // 2. 持ち主の Animator
        // ---------------------------------------------------------
        const auto *animator = m_world->GetComponent<AnimatorComponent>(hb.ownerEntity);
        if (!animator || !animator->currentSequence || animator->isExternalOverride)
        {
            // 持ち主が消えた・シーケンスがないときは判定を閉じる
            //   ムービーがクリップを握っている間も閉じる（攻撃モーションを見せただけでダメージが入るため）
            if (hb.isActive)
            {
                hb.isActive = false;
                HITDIAG_LOG(LogCategory::Game,
                            "[HitDiag/Sync]   OFF  hitbox=%llu (owner/sequence が無効)", hitboxID);
            }
            return;
        }

        const AnimSequence *seq = animator->currentSequence;

        // ---------------------------------------------------------
        // 3. HitBox 区間を探す
        // ---------------------------------------------------------
        const AnimNotifyEvent *activeEv = nullptr; // 見つけた区間（攻撃力などを 4 で書く）

        for (const auto &ev : seq->events)
        {
            // 区間のタグ（ev.tagHash）は見ない。1 キャラが持つ攻撃判定は 1 つなので、どの HitBox 区間でも開く
            if (ev.type != AnimEventType::HitBox)
                continue;

            // 今の時刻ではなく、このステップで進んだ範囲 [previousFrameTimer, currentTimer] と
            //   区間 [startTime, endTime] の重なりで見る
            //   速く再生して 1 ステップで区間を丸ごと飛び越しても、判定を取り逃さない
            const float t0 = animator->previousFrameTimer;
            const float t1 = animator->currentTimer;
            bool        overlaps;
            if (t1 >= t0)
            {
                // 時間が進んだ場合: [t0, t1] と [start, end] が重なるか
                overlaps = (ev.startTime <= t1 && ev.endTime >= t0);
            }
            else
            {
                // ループで先頭に戻ったステップ: 進んだ範囲は [t0, duration] と [0, t1]
                //   どちらかに重なれば有効（こう書くと duration を見なくて済む）
                overlaps = (ev.endTime >= t0) || (ev.startTime <= t1);
            }

            if (overlaps)
            {
                activeEv = &ev;
                break;
            }
        }
        const bool shouldBeActive = (activeEv != nullptr);

        // ---------------------------------------------------------
        // 4. 開閉
        // ---------------------------------------------------------
        if (shouldBeActive)
        {
            // 区間に入ったフレームだけ、多段ヒットの履歴を消す
            if (!hb.isActive)
            {
                HITDIAG_LOG(LogCategory::Game,
                            "[HitDiag/Sync]   ON   hitbox=%llu 履歴クリア(hitCount %u -> 0) "
                            "区間=[%.3f,%.3f] timer=[%.3f -> %.3f]",
                            hitboxID, static_cast<unsigned>(hb.hitCount),
                            activeEv->startTime, activeEv->endTime,
                            animator->previousFrameTimer, animator->currentTimer);

                hb.hitCount = 0;
                std::memset(hb.hitTargets, 0, sizeof(hb.hitTargets));
                hb.isActive = true;
            }

            // シーケンサーで HitBox 区間に入れた値を毎ステップ書く
            //   隣の区間（コンボの次の段）に切り替わっても、その区間の値になる

            // ownerID（誰の攻撃か）はアニメーションの持ち主のキャラにする
            //   自分に当てないための判定と、攻撃した側のヒットストップに使う
            hb.damageAmount    = activeEv->damage;
            hb.knockback       = activeEv->knockback;
            hb.hitStopDuration = activeEv->hitStopDuration;
            hb.hitStopScale    = activeEv->hitStopScale;
            hb.ownerID         = hb.ownerEntity;
        }
        else if (hb.isActive)
        {
            // 区間を抜けた・アニメーションが切り替わった
            hb.isActive = false;

            // 閉じないまま次の攻撃に入ると、履歴が消えず AlreadyHit で当たらなくなる
            //   調査するときは ON と OFF のログが対で出ているかを確かめる
            HITDIAG_LOG(LogCategory::Game,
                        "[HitDiag/Sync]   OFF  hitbox=%llu hitCount=%u timer=[%.3f -> %.3f]",
                        hitboxID, static_cast<unsigned>(hb.hitCount),
                        animator->previousFrameTimer, animator->currentTimer);
        }
    });
}



// AnimationSystem（L02_PostUpdate）の後、当たり判定と同じ L05_Collision に置く
REGISTER_LOGIC_SYSTEM(CombatAnimationSyncSystem, Priority::LogicStage::L05_Collision);
