#pragma once
// ===========================================================================
// 概要: 命中の結果（DamageEventComponent）を処理し、HP を減らしてノックバックを書く System。
//
// 責務:
//   - 無敵中でなければ HP を減らし、無敵時間を始める
//   - 攻撃した側と受けた側にヒットストップをかけ、KnockbackComponent に速度を書く
//   - 画面の演出・カメラの揺れ・被弾と命中のイベント・のけぞりのアニメーションを出す
//   - 処理した DamageEvent のエンティティを消す
//
// 備考: 戦闘の流れ:
//         L05 HitboxCollisionSystem  … 命中を判定して DamageEvent を作る
//         L06 DamageResolutionSystem … この System
//         L06 HealthSystem           … 無敵時間を減らし、倒れたかを判定する
//       VFX やカメラは直接呼ばず、イベントやコンポーネントのデータで渡す。
//       ノックバックは速度を書くだけで、実際に動かして減らすのは JoltCharacterUpdateSystem。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/CombatComponents.h"

class DamageResolutionSystem : public CCL::ECS::IfSystem<DamageResolutionSystem,
                                                         CCL::ECS::Write<DamageEventComponent>>
{
public:
    DamageResolutionSystem() : IfSystem("DamageResolutionSystem") {}
    void Update(float dt) override;
};