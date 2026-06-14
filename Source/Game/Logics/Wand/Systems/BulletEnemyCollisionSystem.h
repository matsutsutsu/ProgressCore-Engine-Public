#pragma once
// ===========================================================================
// Systems/BulletEnemyCollisionSystem.h
// -------------------------------------
// @file
// @brief 弾×敵 判定を空間ハッシュで行うSystem（L06、直列）。
//        弾のセル＋隣接8セルの敵のみ判定。8192発でも O(弾数×近傍敵数) で収まる。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Wand/Data/BulletComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"

class BulletEnemyCollisionSystem
    : public CCL::ECS::IfSystem<BulletEnemyCollisionSystem,
    CCL::ECS::Write<BulletStateComponent>,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<BulletProgramRef>,
    CCL::ECS::Read<BulletTag>>
{
public:
    BulletEnemyCollisionSystem() : IfSystem("BulletEnemyCollisionSystem") {}
    void Update(float dt) override;
};