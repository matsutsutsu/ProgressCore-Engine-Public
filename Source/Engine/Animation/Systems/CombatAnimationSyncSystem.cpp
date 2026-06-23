/**
 * @file CombatAnimationSyncSystem.cpp
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

// ロード時に一度だけ計算するハッシュ定数
// HashString はコンパイル時定数なので実行コストゼロ
static constexpr uint32_t kHitBoxEventHash = CCL::Utils::HashString("HitBox");

void CombatAnimationSyncSystem::Update(float /*dt*/)
{
    // =====================================================================
    // 【イテレーション反転】HitboxComponent を主役にして走査する
    //
    // 旧実装: ルートエンティティ(Animator+Roster) を走査
    //         → 武器エンティティの Hitbox を GetComponent で取得して書き換え
    //         → Write 未宣言のクロスエンティティ書き込み（スケジューラ契約違反）
    //
    // 新実装: HitboxComponent を持つエンティティを直接走査（Write<> 宣言済み）
    //         → ownerEntity で Animator を「読むだけ」（スケジューラ安全）
    //         → tagHash で文字列比較コストをゼロ化（毎フレーム strcmp なし）
    // =====================================================================
    ForEachWithID([this](EntityID hitboxID, HitboxComponent &hb) {
        // ── 1. 駆動元の設定が未初期化なら無視 ──────────────────────────
        // ownerEntity は武器スポーン時に設定される。
        // 設定されていない Hitbox（弾など常時判定のもの）はここでスキップ。
        if (hb.ownerEntity == InvalidEntityID)
            return;

        // ── 2. 駆動元 Animator を取得（別エンティティ・読み取り専用） ──
        const auto *animator = _world->GetComponent<AnimatorComponent>(hb.ownerEntity);
        if (!animator || !animator->currentSequence)
        {
            // オーナーが無効またはシーケンス未設定 → 安全に非アクティブ化
            if (hb.isActive)
            {
                hb.isActive = false;
                CCL_LOG_INFO(LogCategory::Game,
                    "[HitboxSync] Force deactivated (no owner/seq). Entity %llu", hitboxID);
            }
            return;
        }

        const AnimSequence *seq = animator->currentSequence;

        // ── 3. このHitboxに対応する区間が現在アクティブか判定 ───────────
        // tagHash はスポーン時に1回だけ計算済み → 毎フレームの文字列比較なし
        bool shouldBeActive = false;

        for (const auto &ev : seq->events)
        {
            // eventName のハッシュが "HitBox" でなければスキップ
            // ※ ev.eventName は実行時文字列なのでここだけ HashString を呼ぶ。
            //   将来 AnimNotifyEvent に eventHash フィールドを追加すれば完全にゼロコストになる。
            //if (CCL::Utils::HashString(ev.eventName.c_str()) != kHitBoxEventHash)
                continue;

            // stringParam のハッシュと tagHash を比較（整数比較のみ）
            if (CCL::Utils::HashString(ev.stringParam.c_str()) != hb.tagHash)
                continue;

            // 現在の再生時間がイベント区間内か
            if (animator->currentTimer >= ev.startTime &&
                animator->currentTimer <= ev.endTime)
            {
                shouldBeActive = true;
                break;
            }
        }

        // ── 4. エッジ検出 & 書き込み ────────────────────────────────────
        // Write<HitboxComponent> を宣言済み・自エンティティへの書き込みのみ → 安全
        if (shouldBeActive && !hb.isActive)
        {
            // 【立ち上がりエッジ】ON になった瞬間：多段ヒット防止の履歴をクリア
            hb.hitCount = 0;
            std::memset(hb.hitTargets, 0, sizeof(hb.hitTargets));
            hb.isActive = true;

            CCL_LOG_INFO(LogCategory::Game,
                "[HitboxSync] Activated. Entity %llu (tagHash 0x%08X)", hitboxID, hb.tagHash);
        }
        else if (!shouldBeActive && hb.isActive)
        {
            // 【立ち下がりエッジ】区間を抜けた / アニメがキャンセルされた瞬間
            hb.isActive = false;

            CCL_LOG_INFO(LogCategory::Game,
                "[HitboxSync] Deactivated. Entity %llu (tagHash 0x%08X)", hitboxID, hb.tagHash);
        }
    });
}

// AnimationSystem(L02_PostUpdate=250) の完了後、
// HitboxCollisionSystem と同ステージ(L05_Collision=500) で実行。
// ステージの壁により Animator 書き込み系との直列化が保証される。
REGISTER_LOGIC_SYSTEM(CombatAnimationSyncSystem, Priority::LogicStage::L05_Collision);