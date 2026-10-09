#pragma once
// ===========================================================================
// 概要: 剛体追従の突撃（ChargeMode::RigidFollow）。データ陣形（縦割り・挟み込み・横なぎ等）が使う。
//
// 責務:
//   - 形が動いている間は、陣形が動かす targetPosition に毎フレーム一致させる（壁がたわまない）
//   - 形が動き切ったら Idle のバネへ渡し、惰性で収める
//
// 備考:
//   タメ（stateTimer）は使わない。突撃中はずっと当たる（IsDangerous は既定のまま）。
// ===========================================================================
#include "Game/Logics/AI/Drone/Motion/IChargeMotion.h"

namespace CCL::AI::Motion
{
    class RigidFollowMotion : public IChargeMotion
    {
    public:
        void Move(TransformComponent &trans, DroneComponent &drone, float dt,
                  const MotionContext &ctx) const override;
    };

} // namespace CCL::AI::Motion
