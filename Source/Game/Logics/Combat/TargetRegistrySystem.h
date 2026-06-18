#pragma once
// ===========================================================================
// TargetRegistrySystem.h
// ----------------------
// 役割:
//   敵(EnemyComponent + Transform)を毎フレーム走査し、TargetRegistry(World Resource)
//   に (ID, 位置) を登録する。Homing弾や敵AIが参照する共有ターゲットリストを作る。
//
// 責務:
//   - フレーム先頭で TargetRegistry をクリアし、生存中の敵を全登録する。
//
// アーキテクチャ上の位置付け:
//   実行ステージ L02_PreUpdate。これより後の L02_Update で BulletTravelSystem(Homing)
//   が最新のリストを参照できるよう、Update より前で構築を完了させる。
//
// 設計方針:
//   - 死亡予約済み(DeadTag)の敵は登録しない(消えゆく敵を狙わせない)。
//   - 構築は単一スレッド(共有Resourceへの書き込みのため ForEachWithID)。読み取り側の
//     Homing は並列のまま安全(構築済みリストを読むだけ)。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Enemy/EnemyComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

class TargetRegistrySystem
    : public CCL::ECS::IfSystem<TargetRegistrySystem,
    CCL::ECS::Read<EnemyComponent>,
    CCL::ECS::Read<TransformComponent>>
{
public:
    TargetRegistrySystem() : IfSystem("TargetRegistrySystem") {}
    void Update(float dt) override;
};