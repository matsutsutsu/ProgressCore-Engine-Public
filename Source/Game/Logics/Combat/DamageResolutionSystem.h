#pragma once
// ===========================================================================
// DamageResolutionSystem.h
// ------------------------
// 役割:
//   ダメージイベント(DamageEventComponent)を消費し、HPの減算とノックバックの
//   付与を行う「解決」システム。
//
// 責務:
//   - DamageEvent を走査し、無敵時間を加味して HP を減算する。
//   - ノックバック情報があれば対象に KnockbackComponent を付与(速度書き込みのみ)。
//   - 消費したイベントエンティティを即破棄する(燃やして捨てる)。
//
// アーキテクチャ上の位置付け(戦闘パイプライン):
//   [Jolt] 物理衝突 → EventBus
//     │
//   L05 HitboxCollisionSystem  (判定・DamageEvent生成・クリ/ノックバック方向確定)
//     ▼
//   L06 DamageResolutionSystem ← 本ファイル(HP減算・ノックバック付与・イベント破棄)
//     ▼
//   L06 HealthSystem           (無敵タイマー更新・死亡判定)
//
// 設計方針:
//   - VFX/SE等の直接呼び出しは禁止。視覚効果が必要なら VFXRequest 等を別途生成し
//     描画フェーズへデータで流す(疎結合・DOD)。
//   - ノックバックは「速度を書き込む」だけ。減衰の継続処理は KnockbackSystem に委譲し
//     本Systemの責務を「イベント解決」に限定する(単一責任)。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/CombatComponents.h"

class DamageResolutionSystem : public CCL::ECS::IfSystem<DamageResolutionSystem,
    CCL::ECS::Write<DamageEventComponent>> {
public:
    DamageResolutionSystem() : IfSystem("DamageResolutionSystem") {}
    void Update(float dt) override;
};