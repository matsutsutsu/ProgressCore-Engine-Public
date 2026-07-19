#pragma once
/// @file HitEffectSystem.h
/// @brief 攻撃の確定ヒット時に、命中位置へ衝突エフェクト(GPUパーティクルのバースト)を出す演出システム。
///
/// 設計方針:
///   - HitImpactEvent(EventBus) を購読するだけのトリガ受け。ダメージ解決(L06)と演出を疎結合にする。
///   - 見た目はプレハブ Assets/Prefabs/Particles/VFX_HitSpark.json（バーストのスパーク）に集約。
///     デザイナーがエディタで調整でき、コードは「どこに出すか」だけを担う（表現と供給の分離）。
///   - 命中ごとにエミッタを1個 spawn → 位置を直接セット＋UpdateMatrix（原点フラッシュ防止）→ TriggerBurst。
///     短い寿命の後に破棄する（SpawnHitboxSystem の短命エンティティ方式と同じ）。
///   - 連撃が激しい場合はエミッタのプール化に発展できるが、まずは素直な spawn/destroy 方式。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include <vector>

class HitEffectSystem : public CCL::ECS::IfSystem<HitEffectSystem>
{
  public:
    HitEffectSystem()
        : IfSystem("HitEffectSystem")
    {
    }

    void Initialize() override; ///< HitImpactEvent を購読
    void Update(float dt) override; ///< 生成したVFXの寿命管理（期限切れを破棄）

  private:
    struct ActiveVfx
    {
        CCL::ECS::EntityID id   = CCL::ECS::InvalidEntityID;
        float              life = 0.0f; ///< 残り寿命(秒・実時間)
    };
    std::vector<ActiveVfx> _active;

    /// エミッタを保持する時間(秒)。粒子の最大寿命＋余韻より長く取り、消える前に破棄しないようにする。
    static constexpr float kVfxLifetime = 1.0f;
};
