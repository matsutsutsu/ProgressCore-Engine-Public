#pragma once
// ===========================================================================
// 概要: 攻撃が当たったとき、当たった位置に火花のエフェクト（GPU パーティクル）を出す System
//
// 責務:
//   - HitImpactEvent を購読し、VFX::Play でパーティクルを出す
//   - 攻撃した側がラッシュ中のプレイヤーなら、連撃用・とどめ用のパーティクルに差し替える
//
// 備考: 見た目はプレハブ Assets/Prefabs/Particles/ にあり、エディタで調整できる
//       プレハブの生成・発射・粒子が消えた後の片付けは VFXSystem が行うので、この System は
//       出したエンティティを管理しない
//
//       設計上の妥協: 汎用の火花の System が、ラッシュという特定の技だけを知っている
//       攻撃ごとに差し替える仕組み（シーケンサーの HitBox 区間に持たせる）は、ラッシュ以外で
//       差し替える予定が企画上ないので作らない。必要になったらそちらへ移す
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_Types.h"

class HitEffectSystem : public CCL::ECS::IfSystem<
    HitEffectSystem>
{
public:
    HitEffectSystem() : IfSystem("HitEffectSystem") {}

    void Initialize() override;     // HitImpactEvent を購読する
    void Update(float dt) override; // 何もしない（再生と片付けは VFXSystem）

private:
    // 出すパーティクルのプレハブを、攻撃した側の状態で選ぶ
    // ラッシュ中のプレイヤーなら連撃用（とどめのアニメに入った後はとどめ用）、それ以外は通常の火花
    const char *PickHitPrefab(CCL::ECS::EntityID attackerID) const;
};
