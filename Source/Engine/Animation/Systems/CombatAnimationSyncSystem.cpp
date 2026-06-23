/**
 * @file CombatAnimationSyncSystem.cpp
 * @brief HitboxComponent を主役に走査し、ownerEntity の Animator を読んで
 *        攻撃判定の区間状態を Hitbox に同期する（イテレーション反転設計）。
 */
#include "CombatAnimationSyncSystem.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Math/StringHash.h"

using namespace CCL::ECS;

void CombatAnimationSyncSystem::Update(float /*dt*/)
{
    // =====================================================================
    // 【イテレーション反転】HitboxComponent を主役にして走査する
    //
    //   Write<HitboxComponent> を宣言し、自エンティティの Hitbox のみ書き込む。
    //   駆動元(owner)の Animator は別エンティティへの「読み取り専用」参照。
    //   本システムは L05_Collision、Animator 書き込み系は L02_* のため、
    //   ステージの壁により実行順が直列化されレースしない。
    //
    //   旧実装の文字列ハッシュ比較は enum 比較に置換済み（型安全・高速化）。
    // =====================================================================
    ForEachWithID([this](EntityID hitboxID, HitboxComponent &hb) {
        // ── 1. 駆動元未設定の Hitbox（弾など常時判定）はスキップ ──
        if (hb.ownerEntity == InvalidEntityID)
            return;

        // ── 2. 駆動元 Animator を取得（別エンティティ・読み取り専用） ──
        const auto *animator = _world->GetComponent<AnimatorComponent>(hb.ownerEntity);
        if (!animator || !animator->currentSequence)
        {
            // オーナー無効 or シーケンス未設定 → 安全に非アクティブ化
            if (hb.isActive)
            {
                hb.isActive = false;
                CCL_LOG_INFO(LogCategory::Game,
                    "[HitboxSync] Force deactivated (no owner/seq). Entity %llu", hitboxID);
            }
            return;
        }

        const AnimSequence *seq = animator->currentSequence;

        // ── 3. このHitboxに対応する HitBox 区間が現在アクティブか判定 ──
        bool shouldBeActive = false;

        for (const auto &ev : seq->events)
        {
            // enum 比較（旧: HashString(ev.eventName) == "HitBox"）
            if (ev.type != AnimEventType::HitBox)
                continue;

            // 武器タグ一致判定。tagHash はスポーン時に1回計算済み。
            // stringParam のハッシュは小規模(武器数×イベント数)なので毎フレームでも実質ゼロコスト。
            if (CCL::Utils::HashString(ev.stringParam.c_str()) != hb.tagHash)
                continue;

            if (animator->currentTimer >= ev.startTime &&
                animator->currentTimer <= ev.endTime)
            {
                shouldBeActive = true;
                break;
            }
        }

        // ── 4. エッジ検出 & 書き込み（Write宣言済み・自エンティティ） ──
        if (shouldBeActive && !hb.isActive)
        {
            // 【立ち上がりエッジ】多段ヒット防止の履歴をクリア
            hb.hitCount = 0;
            std::memset(hb.hitTargets, 0, sizeof(hb.hitTargets));
            hb.isActive = true;

            CCL_LOG_INFO(LogCategory::Game,
                "[HitboxSync] Activated. Entity %llu (tagHash 0x%08X)", hitboxID, hb.tagHash);
        }
        else if (!shouldBeActive && hb.isActive)
        {
            // 【立ち下がりエッジ】区間を抜けた / アニメがキャンセルされた
            hb.isActive = false;

            CCL_LOG_INFO(LogCategory::Game,
                "[HitboxSync] Deactivated. Entity %llu (tagHash 0x%08X)", hitboxID, hb.tagHash);
        }
    });
}

// AnimationSystem(L02_PostUpdate=250) 完了後、衝突判定(L05_Collision=500)と同ステージ。
// ステージの壁により Animator 書き込み系との直列化が保証される。
REGISTER_LOGIC_SYSTEM(CombatAnimationSyncSystem, Priority::LogicStage::L05_Collision);