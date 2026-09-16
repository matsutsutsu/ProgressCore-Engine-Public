// TransformSnapshotSystem.h
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"

/**
 * @brief 描画補間のため、毎FixedUpdateの最初に position/rotation を
 *        prevPosition/prevRotation へ退避するシステム。
 * @note Fix Your Timestep パターンの「previousState = currentState」に相当。
 *       これが無いと、物理が複数ステップ進んだフレームで補間の起点がズレる。
 * @warning 入力・物理より前に必ず実行すること（L01_Input と同優先度で並列可）。
 */
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
