#pragma once
// ===========================================================================
// 概要: 描画の補間のため、固定ステップの最初に position / rotation を
//       prevPosition / prevRotation へ写す System
//
// 備考: 描画フレームごとでなく、固定ステップごとに写す
//       描画フレームごとだと、1フレームで複数ステップ進んだときに補間の起点が2ステップ以上前になり、絵が飛ぶ
//       入力・物理より前（L01_Input）に走らせる。後だと、そのステップで動いた後の値を写してしまう
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"

class TransformSnapshotSystem : public CCL::ECS::IfSystem<
    TransformSnapshotSystem,
    CCL::ECS::Write<TransformComponent>>
{
public:
    TransformSnapshotSystem() : IfSystem("TransformSnapshotSystem") {}
    virtual ~TransformSnapshotSystem() = default;

    void Update(float dt) override;
};
