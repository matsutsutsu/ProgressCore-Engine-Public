#pragma once
// ===========================================================================
// 概要: 描画補間のため、毎 FixedUpdate の最初に position/rotation を
//       prevPosition/prevRotation へ退避する System。
//
// 責務:
//   - 各ステップの開始時点の姿勢を控えておく（補間の起点）
//
// 備考: Fix Your Timestep パターンの「previousState = currentState」に相当する。
//       これが無いと、物理が複数ステップ進んだフレームで補間の起点がずれる。
//       入力・物理より前（L01_Input）に必ず実行すること。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"

class TransformSnapshotSystem : public CCL::ECS::IfSystem<TransformSnapshotSystem,
                                                          CCL::ECS::Write<TransformComponent>>
{
public:
    TransformSnapshotSystem()
        : IfSystem("TransformSnapshotSystem")
    {
    }
    virtual ~TransformSnapshotSystem() = default;

    void Update(float dt) override;
};
