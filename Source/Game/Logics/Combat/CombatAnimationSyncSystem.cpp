/**
 * @file CombatAnimationSyncSystem.cpp
 * @brief HitboxComponent を主役に走査し、ownerEntity の Animator を読んで
 *        攻撃判定の区間状態を Hitbox に同期する（イテレーション反転設計）。
 */
#include "CombatAnimationSyncSystem.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Physics/JoltIntegration/HitDiagnostics.h" // 計測ログ(暫定)

using namespace CCL::ECS;

namespace
{
    // Hitbox エンティティから親を辿り、最初に見つかった Animator 保持エンティティを返す。
    // 剣はキャラ(=Animatorを持つ)の子孫にぶら下がる前提。空間生成の判定球は親を持たない
    // ため見つからず InvalidEntityID を返す（＝常時判定の弾のまま）。
    EntityID ResolveAnimatorOwner(CCL::ECS::World* world, EntityID hitboxID)
    {
        EntityID cur = InvalidEntityID;
        if (const auto* t = world->GetComponent<TransformComponent>(hitboxID))
            cur = t->parent; // EntityLink → EntityID 暗黙変換
        int guard = 0;
        while (cur != InvalidEntityID && cur != 0 && guard++ < 32)
        {
            if (world->GetComponent<AnimatorComponent>(cur))
                return cur; // 台本の持ち主を発見
            const auto* pt = world->GetComponent<TransformComponent>(cur);
            cur = pt ? static_cast<EntityID>(pt->parent) : InvalidEntityID;
        }
        return InvalidEntityID;
    }
} // namespace

void CombatAnimationSyncSystem::Update(float /*dt*/)
{
    // =====================================================================
    // 【イテレーション反転】HitboxComponent を主役にして走査する
    //
    //   Write<HitboxComponent> を宣言し、自エンティティの Hitbox のみ書き込む。
    //   駆動元(owner)の Animator は別エンティティへの「読み取り専用」参照。
    //   本システムは L05_Collision、Animator 書き込み系は L02_* のため、
    //   ステージの壁により実行順が直列化されレースしない。
    // =====================================================================
    ForEachWithID([this](EntityID hitboxID, HitboxComponent &hb) {
        // ── 1. 駆動元(owner)の解決 ──
        //   未設定なら親を辿って Animator 保持の祖先(=キャラ)を自動解決しキャッシュする。
        //   剣はキャラの子孫なので解決できる。空間生成の判定球は親が無く解決不能＝
        //   ownerEntity=Invalid のまま「常時判定の弾」として下でスキップされる。
        if (hb.ownerEntity == InvalidEntityID)
            hb.ownerEntity = ResolveAnimatorOwner(m_world, hitboxID);

        if (hb.ownerEntity == InvalidEntityID)
            return;

        // ── 2. 駆動元 Animator を取得（別エンティティ・読み取り専用） ──
        const auto *animator = m_world->GetComponent<AnimatorComponent>(hb.ownerEntity);
        if (!animator || !animator->currentSequence)
        {
            // オーナー無効 or シーケンス未設定 → 安全に非アクティブ化
            if (hb.isActive)
            {
                hb.isActive = false;
                HITDIAG_LOG(LogCategory::Game,
                    "[HitDiag/Sync]   OFF  hitbox=%llu (owner/sequence が無効)", hitboxID);
            }
            return;
        }

        const AnimSequence *seq = animator->currentSequence;

        // ── 3. このHitboxに対応する HitBox 区間が現在アクティブか判定 ──
        //   アクティブなイベントを捕まえておき、そのイベントに入力された戦闘パラメータ
        //   （攻撃力・ヒットストップ）を Hitbox へ反映する。
        const AnimNotifyEvent *activeEv = nullptr;

        for (const auto &ev : seq->events)
        {
            // enum 比較
            if (ev.type != AnimEventType::HitBox)
                continue;

            // タグ照合: hb.tagHash==0(未設定)なら「いずれの HitBox 区間でも」有効にする
            // （単一武器の簡易運用）。タグが設定されている場合のみ厳密照合する。
            if (hb.tagHash != 0 && ev.tagHash != hb.tagHash)
                continue;

            // 区間スイープ判定：点包含ではなく [previousFrameTimer, currentTimer] と
            //   [startTime, endTime] の重なりで判定する。これにより、高速再生や処理落ちで
            //   1フレームの進み幅が区間を丸ごと飛び越しても、その窓を跨いだフレームで
            //   確実にアクティブになる（＝短い/高速な攻撃判定が空振りしない）。
            const float t0 = animator->previousFrameTimer;
            const float t1 = animator->currentTimer;
            bool        overlaps;
            if (t1 >= t0)
            {
                // 通常（単調増加）: 進んだ区間 [t0,t1] と [start,end] が重なるか
                overlaps = (ev.startTime <= t1 && ev.endTime >= t0);
            }
            else
            {
                // ループで巻き戻ったフレーム: 進んだ範囲は [t0,duration]∪[0,t1]。
                // どちらかに重なれば有効（duration を参照せずに判定できる）。
                overlaps = (ev.endTime >= t0) || (ev.startTime <= t1);
            }

            if (overlaps)
            {
                activeEv = &ev;
                break;
            }
        }
        const bool shouldBeActive = (activeEv != nullptr);

        // ── 4. エッジ検出 & 書き込み（Write宣言済み・自エンティティ） ──
        if (shouldBeActive)
        {
            // 【立ち上がりエッジ】多段ヒット防止の履歴をクリア（区間に入った最初の1回だけ）
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

            // Sequencer の HitBox イベントに入力した値を毎フレーム Hitbox へ反映する。
            //   これが無いと攻撃力/ヒットストップが 0 のままで当たっても効かない。
            //   毎フレーム反映にすることで、隣接する別区間(コンボ)へ切り替わっても
            //   その区間の値が正しく入る。攻撃帰属(ownerID)はアニメの持ち主＝キャラ本体
            //   （自傷防止・攻撃側ヒットストップの適用先）。
            hb.damageAmount    = activeEv->damage;
            hb.knockback       = activeEv->knockback;
            hb.hitStopDuration = activeEv->hitStopDuration;
            hb.hitStopScale    = activeEv->hitStopScale;
            hb.ownerID         = hb.ownerEntity;
        }
        else if (hb.isActive)
        {
            // 【立ち下がりエッジ】区間を抜けた / アニメがキャンセルされた
            hb.isActive = false;

            // ここが出ないまま次の攻撃に入ると、履歴クリアが走らず AlreadyHit で
            // 弾かれ続ける。ON と OFF が対で出ているかを必ず確認すること。
            HITDIAG_LOG(LogCategory::Game,
                "[HitDiag/Sync]   OFF  hitbox=%llu hitCount=%u timer=[%.3f -> %.3f]",
                hitboxID, static_cast<unsigned>(hb.hitCount),
                animator->previousFrameTimer, animator->currentTimer);
        }
    });
}

// AnimationSystem(L02_PostUpdate=250) 完了後、衝突判定(L05_Collision=500)と同ステージ。
// ステージの壁により Animator 書き込み系との直列化が保証される。
REGISTER_LOGIC_SYSTEM(CombatAnimationSyncSystem, Priority::LogicStage::L05_Collision);
