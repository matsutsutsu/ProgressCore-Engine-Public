#pragma once
// ===========================================================================
// Bullet/BulletHitResponseSystem.h
// --------------------------------
// @file
// @brief JoltCollisionEvent を購読し「弾 × 敵」の命中を処理するSystem（L05）。
//        ダメージ適用と破棄フラグ(hitEnemy)を立てる。破棄/継承は L07 が行う。
// ===========================================================================
#include "Engine/ECS/System/CCL_System.h"

class BulletHitResponseSystem : public CCL::ECS::IfSystem<BulletHitResponseSystem>
{
public:
    BulletHitResponseSystem() : IfSystem("BulletHitResponseSystem") {}
    void Initialize() override; // EventBus 購読登録（Subscribe<JoltCollisionEvent>）
    void Update(float dt) override;
};