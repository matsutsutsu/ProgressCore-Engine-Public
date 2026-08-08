#pragma once
/// @file HitEffectSystem.h
/// @brief 攻撃の確定ヒット時に、命中位置へ衝突エフェクト(GPUパーティクルのバースト)を出す演出システム。
///
/// 設計方針:
///   - HitImpactEvent(EventBus) を購読するだけのトリガ受け。ダメージ解決(L06)と演出を疎結合にする。
///   - 見た目はプレハブ Assets/Prefabs/Particles/VFX_HitSpark.json（バーストのスパーク）に集約。
///     デザイナーがエディタで調整でき、コードは「どこに出すか」だけを担う（表現と供給の分離）。
///   - 再生は Vfx::Play の1行に委ねる。プールからの貸し出し・姿勢の即時確定・バースト発火・
///     寿命後の返却はすべて VfxSystem の責務であり、本システムは寿命も実体も管理しない。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_Types.h"

class HitEffectSystem : public CCL::ECS::IfSystem<HitEffectSystem>
{
  public:
    HitEffectSystem()
        : IfSystem("HitEffectSystem")
    {
    }

    void Initialize() override;     ///< HitImpactEvent を購読
    void Update(float dt) override; ///< 毎フレームの仕事は無い（再生と寿命は VfxSystem 側）
};
