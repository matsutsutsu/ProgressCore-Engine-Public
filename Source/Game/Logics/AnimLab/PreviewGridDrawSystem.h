#pragma once
// ===========================================================================
// 概要: PreviewGridComponent の目盛りを床に線で描く System（エディタのみ）
//
// 備考: AnimLab の床に付けて使う。中心と、前後（Z）・左右（X）の軸の線は色を変え、キャラの向きと前進方向を読めるようにする
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AnimLab/PreviewGridComponent.h"

class ShapeRenderer;

class PreviewGridDrawSystem : public CCL::ECS::IfSystem<
    PreviewGridDrawSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<PreviewGridComponent>>
{
public:
    PreviewGridDrawSystem() : IfSystem("PreviewGridDrawSystem") {}

    void Update(float dt) override;

private:
    void DrawGrid(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &center, const PreviewGridComponent &grid) const;
};
