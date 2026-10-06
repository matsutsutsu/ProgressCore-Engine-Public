#pragma once
// ===========================================================================
// 概要: 攻撃が当たったとき、当たった位置に火花のエフェクト（GPU パーティクル）を出す System。
//
// 責務:
//   - HitImpactEvent を購読し、VFX::Play で VFX_HitSpark を出す
//
// 備考: 見た目はプレハブ Assets/Prefabs/Particles/VFX_HitSpark.json にあり、エディタで調整できる。
//       プレハブの生成・発射・粒子が消えた後の片付けは VFXSystem が行うので、この System は
//       出したエンティティを管理しない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_Types.h"

class HitEffectSystem : public CCL::ECS::IfSystem<HitEffectSystem>
{
public:
    HitEffectSystem() : IfSystem("HitEffectSystem") {}

    void Initialize() override;     // HitImpactEvent を購読する
    void Update(float dt) override; // 何もしない（再生と片付けは VFXSystem）
};
