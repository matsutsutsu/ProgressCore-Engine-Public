#pragma once
// ===========================================================================
// BulletEnemyCollisionSystem.h
// ----------------------------
// 役割:
//   弾が敵に当たったときの OnHit を解決する。貫通でなければ弾に hitEnemy を立て、
//   L07 の BulletInheritanceSystem に破棄させる。壁担当の BulletWallCollisionSystem
//   と対になる「敵担当」のOnHitシステム。
//
// 責務:
//   - JoltCollisionEvent(弾×敵)を購読し、弾側の OnHit(貫通可否)を解釈する。
//   - 貫通でなければ hitEnemy=true（弾の生死のみを扱う）。ダメージは扱わない。
//
// アーキテクチャ上の位置付け(OnHitフェーズ):
//   ダメージは HitboxCollisionSystem(L05) が DamageEvent 経由で別途処理する。
//   本System は「弾が敵に当たって消えるか貫通するか」だけを担当し、責務を分離する。
//     BulletWallCollisionSystem  … 壁に対するOnHit(貫通/反射/消滅)
//     BulletEnemyCollisionSystem … 敵に対するOnHit(貫通/消滅) ← 本ファイル
//
// 設計方針:
//   - HitboxCollisionSystem に弾の生死を持たせない(ダメージと生死の密結合を避ける)。
//   - 多段ヒット(貫通弾が複数の敵を貫く)は Hitbox 側の hitTargets が担保するため、
//     ここでは「貫通なら hitEnemy を立てない」だけでよい。
// ===========================================================================
#include "Engine/ECS/System/CCL_System.h"

class BulletEnemyCollisionSystem : public CCL::ECS::IfSystem<BulletEnemyCollisionSystem>
{
public:
    BulletEnemyCollisionSystem() : IfSystem("BulletEnemyCollisionSystem") {}
    void Initialize() override; // JoltCollisionEvent 購読
    void Update(float dt) override;
};