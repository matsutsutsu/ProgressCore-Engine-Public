#pragma once
#include "ECS/System/CCL_System.h"
#include "Game/Logics/Combat/Weapon/Core/BulletContextComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Utils/TimerComponent.h"
#include "Game/Logics/Combat/Weapon/Attachments/BulletHitStateComponent.h"

/* ==============================================================================
 * [CORE ARCHITECTURE: ECS System Pipeline (弾丸のライフサイクル)]
 * 役割: 物理干渉、ダメージ計算、アタッチメント効果の適用と寿命管理。
 *
 * [弾丸システムの厳格な実行順序 (Data Flow)]
 * 1. BulletRaycastSystem    : 壁判定 -> hitState.isFatal = true を立てる(消滅予定)
 * 2. BulletCollisionSystem  : 敵判定 -> HP減算、貫通計算、hitState情報の更新
 * 3. TravelSystems          : 飛行中処理 (誘導、加速、分裂など)
 * 4. OnHitSystems           : 命中時処理 (爆発生成や、反射によるisFatalフラグの折損)
 * 5. BulletUpdateSystem     : 最終処理 -> 寿命減算、継承(Endモッドの次世代発射)、プール返却
 *
 * 思想 (データ指向):
 * System間で直接関数を呼び出す(Call)ことは絶対にしない。
 * すべての連携は「BulletHitStateComponent」などのフラグ(データ)を介して行われる。
 * ============================================================================== */

class BulletUpdateSystem : public CCL::ECS::IfSystem<BulletUpdateSystem,
                               CCL::ECS::Write<BulletContextComponent>,
    CCL::ECS::Write<RigidBodyComponent>,
    CCL::ECS::Write<TransformComponent>, CCL::ECS::Write<TimerComponent>,
    CCL::ECS::Write<BulletHitStateComponent>> {
  public:
    BulletUpdateSystem() : IfSystem("BulletUpdateSystem") {}

    void Update(float dt) override;

};