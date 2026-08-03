// ===========================================================================
// TrailAnimationSyncSystem.cpp
// 役割・設計方針は TrailAnimationSyncSystem.h を参照。
//
// 実装の意図(Why):
//   - owner 解決は「親を辿って最初の Animator 保持エンティティ」。武器は必ずキャラの
//     子孫にぶら下がる前提なので、これで確実に台本の持ち主へ辿り着く。
//   - 解決できたら ownerEntity にキャッシュし、以降は親辿りをしない。未解決(Invalid)の
//     間は毎フレーム再挑戦する（階層が後から組まれるケースに追従。上限32でループ保護）。
//   - タグ絞り込みは行わず owner の「いずれかの Trail 区間」で ON。単一武器のデモでは
//     これで十分。二刀流でタグ別に出し分けたい場合は ev.tagHash との照合を足す。
// ===========================================================================
#include "TrailAnimationSyncSystem.h"

#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void TrailAnimationSyncSystem::Update(float /*dt*/)
{
    ForEachWithID([this](EntityID /*id*/, const TransformComponent& transform, TrailComponent& trail) {

        // 手動運用の Trail には触れない（emitting はインスペクタ/常時ON のまま）。
        if (!trail.animControlled)
            return;

        // ── 1. owner(Animator を持つ祖先)を解決・キャッシュ ──
        if (trail.ownerEntity == InvalidEntityID)
        {
            EntityID cur   = transform.parent; // EntityLink → EntityID 暗黙変換
            int      guard = 0;
            while (cur != InvalidEntityID && cur != 0 && guard++ < 32)
            {
                if (m_world->GetComponent<AnimatorComponent>(cur))
                    break; // 台本の持ち主を発見
                const auto* pt = m_world->GetComponent<TransformComponent>(cur);
                cur = pt ? static_cast<EntityID>(pt->parent) : InvalidEntityID;
            }
            trail.ownerEntity = (cur != 0) ? cur : InvalidEntityID;
            // 見つからなければ Invalid のまま。次フレーム再挑戦する。
        }

        // ── 2. owner の Animator を読む（別エンティティ・読み取り専用） ──
        const AnimatorComponent* animator =
            (trail.ownerEntity != InvalidEntityID)
                ? m_world->GetComponent<AnimatorComponent>(trail.ownerEntity)
                : nullptr;

        if (!animator || !animator->currentSequence)
        {
            trail.emitting = false; // 台本なし → 発生停止（安全側）
            return;
        }

        // ── 3. 現在時刻が Trail 区間内かを判定して emitting へ反映 ──
        const AnimSequence* seq = animator->currentSequence;
        bool inside = false;
        for (const auto& ev : seq->events)
        {
            if (ev.type != AnimEventType::Trail)
                continue;
            if (animator->currentTimer >= ev.startTime &&
                animator->currentTimer <= ev.endTime)
            {
                inside = true;
                break;
            }
        }
        trail.emitting = inside;
    });
}

// AnimationSystem(L02_PostUpdate) の後、描画の TrailSystem(R08) の前に走る。
REGISTER_LOGIC_SYSTEM(TrailAnimationSyncSystem, Priority::LogicStage::L05_Collision);
