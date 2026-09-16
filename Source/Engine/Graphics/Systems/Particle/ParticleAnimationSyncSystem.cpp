// ===========================================================================
// ParticleAnimationSyncSystem.cpp
// 役割・設計方針は ParticleAnimationSyncSystem.h を参照。
//
// 実装の意図(Why):
//   - owner 解決は「親を辿って最初の Animator 保持エンティティ」。エミッタは剣ボーン等の
//     子孫にぶら下がる前提なので、これで確実に台本の持ち主へ辿り着く。
//   - 解決できたら ownerEntity にキャッシュし、以降は親辿りをしない。未解決(Invalid)の
//     間は毎フレーム再挑戦する（階層が後から組まれるケースに追従。上限32でループ保護）。
//   - タグ絞り込みは行わず owner の「いずれかの Particle 区間」で ON。単一エミッタの運用では
//     これで十分。複数エミッタをタグ別に出し分けたい場合は ev.tagHash との照合を足す。
//   - emitting=false の反映先は GPU 側の emit_enabled（GPUParticleUpdateSystem 経由）。
//     停止しても既存粒子は寿命まで生きて自然に消えるため、区間終端で尾を引いて消える。
// ===========================================================================
#include "ParticleAnimationSyncSystem.h"

#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void ParticleAnimationSyncSystem::Update(float /*dt*/)
{
    ForEachWithID([this](EntityID /*id*/, const TransformComponent &transform, GPUParticleComponent &particle) {
        // 手動運用のエミッタには触れない（emitting はインスペクタ/常時ON のまま）。
        if (!particle.animControlled)
            return;

        // ── 1. owner(Animator を持つ祖先)を解決・キャッシュ ──
        if (particle.ownerEntity == InvalidEntityID)
        {
            EntityID cur   = transform.parent; // EntityLink → EntityID 暗黙変換
            int      guard = 0;
            while (cur != InvalidEntityID && cur != 0 && guard++ < 32)
            {
                if (m_world->GetComponent<AnimatorComponent>(cur))
                    break; // 台本の持ち主を発見
                const auto *pt = m_world->GetComponent<TransformComponent>(cur);
                cur            = pt ? static_cast<EntityID>(pt->parent) : InvalidEntityID;
            }
            particle.ownerEntity = (cur != 0) ? cur : InvalidEntityID;
            // 見つからなければ Invalid のまま。次フレーム再挑戦する。
        }

        // ── 2. owner の Animator を読む（別エンティティ・読み取り専用） ──
        const AnimatorComponent *animator =
            (particle.ownerEntity != InvalidEntityID)
                ? m_world->GetComponent<AnimatorComponent>(particle.ownerEntity)
                : nullptr;

        if (!animator || !animator->currentSequence)
        {
            particle.SetEmitting(false); // 台本なし → 全エミッタ発生停止（安全側）
            return;
        }

        // ── 3. 現在時刻が Particle 区間内かを判定して emitting へ反映 ──
        const AnimSequence *seq    = animator->currentSequence;
        bool                inside = false;
        for (const auto &ev : seq->events)
        {
            if (ev.type != AnimEventType::Particle)
                continue;
            if (animator->currentTimer >= ev.startTime &&
                animator->currentTimer <= ev.endTime)
            {
                inside = true;
                break;
            }
        }
        // 区間の内外を全エミッタへ一括反映する。タグ別に出し分けたくなったら、
        //   ev.tagHash と各エミッタを対応づける仕組みをここに足す。
        particle.SetEmitting(inside);
    });
}

// AnimationSystem(L02_PostUpdate) の後、描画の GPUParticleUpdateSystem(R07) の前に走る。
REGISTER_LOGIC_SYSTEM(ParticleAnimationSyncSystem, Priority::LogicStage::L05_Collision);
