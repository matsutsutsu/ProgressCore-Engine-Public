#pragma once
// ===========================================================================
// 概要: ナビメッシュと、各エージェントの経路・半径をデバッグ描画する System。
//
// 責務:
//   - ナビメッシュのポリゴン（青い面と黒い境界線）を描く
//   - NavAgentComponent を持つ Entity の残り経路（赤線）と半径（黄色い円）を描く
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

class NavMeshDebugDrawSystem : public CCL::ECS::IfSystem<NavMeshDebugDrawSystem>
{
public:
    NavMeshDebugDrawSystem() : IfSystem("NavMeshDebugDrawSystem")
    {
        // 生成時から描画を ON にしておく（System Manager の Debug 列で切り替えられる）
        isDebugVisible = true;
    }

    void Update(float dt) override;
};