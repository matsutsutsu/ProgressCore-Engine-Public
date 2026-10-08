#pragma once
// ===========================================================================
// 概要: 経路探索を要求された Entity について、ナビメッシュ上の経路（waypoints）を計算する System。
//
// 責務:
//   - 現在地と目的地の最寄りポリゴンを探し、A* でポリゴン列を求める
//   - String Pulling で曲がり角だけの waypoints に直して NavAgentComponent へ書く
//
// 備考: 現在は System として登録していない（.cpp 末尾の TODO 参照）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/AI/Navigation/Components/NavAgentComponent.h"

class NavPathfindingSystem : public CCL::ECS::IfSystem<NavPathfindingSystem,
                                                       CCL::ECS::Read<TransformComponent>,
                                                       CCL::ECS::Write<NavAgentComponent>>
{
public:
    NavPathfindingSystem() : IfSystem("NavPathfindingSystem") {}

    // 基底（SystemBase）のデストラクタが virtual なので、override を付けても付けなくても意味は同じ
    virtual ~NavPathfindingSystem() = default;

    void Update(float dt) override;
};