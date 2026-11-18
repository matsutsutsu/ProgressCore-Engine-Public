#include "Game/Logics/Combat/Systems/CombatAnimationSyncSystem.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimOwnerUtils.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Physics/JoltIntegration/HitDiagnostics.h"

using namespace CCL::ECS;



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
            const auto *transform = m_world->GetComponent<TransformComponent>(hitboxID);
            hb.ownerEntity        = transform ? CCL::Anim::FindAnimatorAncestor(*m_world, *transform) : InvalidEntityID;
            hb.ownerResolved      = true;
        }

        // 持ち主のいない判定（SpawnHitboxSystem の球など）は、ここで開閉せず寿命まで有効なままにする
        if (hb.ownerEntity == InvalidEntityID) return;

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
        // このステップで進んだ範囲に重なる区間を探す（見つけた区間の攻撃力などを 4 で書く）
        //   1 キャラが持つ攻撃判定は 1 つなので、どの HitBox 区間でも開く
        const AnimNotifyEvent *activeEv =
            FindOverlappingEvent(*seq, AnimEventType::HitBox, animator->previousFrameTimer, animator->currentTimer);
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
