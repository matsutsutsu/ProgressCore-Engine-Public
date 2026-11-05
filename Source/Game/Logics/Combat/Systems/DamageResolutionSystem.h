#pragma once
// ===========================================================================
// 概要: 命中の結果（DamageEventComponent）を処理し、HP を減らしてノックバックを書く System
//
// 責務:
//   - 無敵中でなければ HP を減らし、無敵時間を始める
//   - 攻撃した側と受けた側にヒットストップをかけ、KnockbackComponent に速度を書く
//   - 画面の演出・カメラの揺れ・被弾と命中のイベント・のけぞりのアニメーションを出す
//   - 処理した DamageEvent のエンティティを消す
//
// 備考: 戦闘の流れは L05 HitboxCollisionSystem → L06 DamageResolutionSystem → L06 HealthSystem
//       VFX やカメラは直接呼ばず、イベントとコンポーネントのデータで渡す
//       ノックバックは速度を書くだけで、動かして減らすのは JoltCharacterUpdateSystem
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/CombatComponents.h"

class DamageResolutionSystem : public CCL::ECS::IfSystem<
    DamageResolutionSystem,
    CCL::ECS::Write<DamageEventComponent>>
{
public:
    DamageResolutionSystem() : IfSystem("DamageResolutionSystem") {}
    void Update(float dt) override;
};