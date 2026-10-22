#pragma once
// ===========================================================================
// 概要: 描画補間のため、毎 FixedUpdate の最初に position/rotation を
//       prevPosition/prevRotation へ退避する System。
//
// 責務:
//   - 各ステップの開始時点の姿勢を控えておく（補間の起点）
//
// 備考:
//   - Fix Your Timestep パターンの「previousState = currentState」に相当する。
//   - 描画フレームごとではなく、固定ステップごとに退避する。1フレームに複数ステップ進んだとき、
//     描画フレームごとの退避では起点が2ステップ以上前になり、補間が1ステップ分を超えて飛ぶため。
//   - 入力・物理より前（L01_Input）に必ず実行すること。後だとそのステップで動いた後の値を退避してしまう。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"

class TransformSnapshotSystem : public CCL::ECS::IfSystem<TransformSnapshotSystem,
                                                          CCL::ECS::Write<TransformComponent>>
{
public:
    TransformSnapshotSystem() : IfSystem("TransformSnapshotSystem") {}
    virtual ~TransformSnapshotSystem() = default;

    void Update(float dt) override;
};
