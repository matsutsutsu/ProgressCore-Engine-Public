#pragma once
// ===========================================================================
// 概要: 直進の突撃（ChargeMode::Straight）。弾幕が使う。
//
// 責務:
//   - 発射の順番待ち（fireDelay）の間は持ち場を追う
//   - タメ: 一度止まってから撃つ方向へ加速する
//   - リリース: 撃つ方向へ高速で直進する。当たり判定が開くのはこの間だけ
//   - 突撃の開始時に、タメの時間と撃つ方向を入れる（BeginStraightCharge）
//
// 備考:
//   BeginStraightCharge は技の突撃の始め方（IFormation::OnChargeBegin）の既定。
//   技が別の始め方を書かなければ、突撃はここから始まる。
//
//   撃つ方向（fireDirection）は陣形が書き、書かなかったときだけ BeginStraightCharge が決める。
//   ここでは向きを変えない（ホーミングしない）。
// ===========================================================================
#include "Game/Logics/AI/Drone/Motion/IChargeMotion.h"

namespace CCL::AI::Motion
{
    /**
     * @brief 直進の突撃を始める。FireCharge へ入った瞬間に1回だけ呼ぶ。
     *
     * 移動方式を直進にし、タメの時間を入れ、撃つ方向が無ければ決める。
     * ここで方向を固定するので、タメ→リリースは方向を変えない直進になる（ホーミングしない）。
     *
     * @param drone 突撃を始める機体。
     * @param info  プレイヤーの位置など。陣形が撃つ方向を書かなかったときの狙いに使う。
     */
    void BeginStraightCharge(DroneComponent &drone, const ChargeBeginInfo &info);

    class StraightChargeMotion : public IChargeMotion
    {
    public:
        void Move(TransformComponent &trans, DroneComponent &drone, float dt,
                  const MotionContext &ctx) const override;

        // タメ（stateTimer > 0）の間は当たらない。当たるのは、リリースで直進しているときだけ。
        bool IsDangerous(const DroneComponent &drone) const override { return drone.stateTimer <= 0.0f; }
    };

} // namespace CCL::AI::Motion
