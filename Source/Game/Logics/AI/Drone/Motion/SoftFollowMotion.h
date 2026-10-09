#pragma once
// ===========================================================================
// 概要: バネ追従の突撃（ChargeMode::SoftFollow）。小隊突撃が使う。
//
// 責務:
//   - 隊列を組んだまま、持ち場へバネで寄る。剛体追従と違って形が歪む
//   - 機首を、目標ではなく実際の進行方向へ向ける
//
// 備考:
//   歪むことがこの技の見た目の要点。目標へバネで寄るので、速いほど後ろへ流れ、曲がると遅れて曲がる。
//   剛体追従にすると隊列がまったく歪まず、「1枚の板が近づいてくる」絵になる。
//
//   タメ（stateTimer）は使わない。突撃中はずっと当たる（IsDangerous は既定のまま）。
//
//   バネの強さは小隊の調整値（SquadTuning::followGain）を BeginFrame で写し取る。
//   後ろの席ほど弱める倍率は、小隊の陣形が drone.springScale に書く。
// ===========================================================================
#include "Game/Logics/AI/Drone/Motion/IChargeMotion.h"

namespace CCL::AI::Motion
{
    class SoftFollowMotion : public IChargeMotion
    {
    public:
        void BeginFrame(CCL::ECS::World &world) override;

        void Move(TransformComponent &trans, DroneComponent &drone, float dt,
                  const MotionContext &ctx) const override;

    private:
        float m_followGain = 10.0f; // バネの強さ。SquadTuning が無いときはこの既定値
    };

} // namespace CCL::AI::Motion
