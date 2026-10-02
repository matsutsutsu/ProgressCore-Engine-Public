#pragma once
// ===========================================================================
// 概要: 攻撃しない中立の陣形（降下円陣・頭上円陣）と、頭上の待機リング。
//
// 責務:
//   - OrbitCircle: ボスの周りを低い高さで周回する
//   - HighOrbit: ボスの頭上高くで周回する（攻撃と攻撃のあいだに使う）
//   - HighRing: 頭上円陣と弾幕が共有する待機リングの位置計算
//   - 攻撃で使った値（attackProgress・fireDelay・chargeMode）を戻す
// ===========================================================================
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include <cmath>

namespace CCL::AI::Formation::Basic
{
    // 頭上の待機リング。頭上円陣と弾幕が共有する。
    //
    // 半径と高さを技側へコピーしない。
    // コピーすると、いつか片方だけ直って、切り替えの瞬間に群れが横滑りする。
    //
    // 回転位相まで同じ式を通すのが要点。
    // ctx.time の項が違うと輪は別の位置にあり、陣形を切り替えた瞬間に全機が並び直してしまう。
    struct HighRing
    {
        static constexpr float kRadius = 20.0f; // 円の半径 [m]（固定。機数に依らない）
        static constexpr float kHeight = 20.0f; // ボス頭上の高さ [m]（地上の間合いを塞がない）
        static constexpr float kSpin   = 0.5f;  // 回転速度 [rad/s]

        // ボス位置からの相対オフセット。ボス位置を足せば持ち場の座標になる。
        static DirectX::SimpleMath::Vector3 SlotOffset(uint16_t localIndex,
                                                       uint16_t totalDrones,
                                                       float    time)
        {
            const int   total     = (totalDrones > 0) ? totalDrones : 1;
            const float baseAngle = (2.0f * 3.14159265f) *
                                    (static_cast<float>(localIndex) / static_cast<float>(total));
            const float angle     = baseAngle + time * kSpin;

            return {std::cos(angle) * kRadius, kHeight, std::sin(angle) * kRadius};
        }
    };

    // 円陣（OrbitCircle）: ボスを中心に円を描いて並ぶ。
    struct OrbitCircleFormation : public IFormation
    {
        // 隊形（位置取り）であって技ではないので、予告の震えを出さない。
        bool UsesShiverTelegraph() const override { return false; }

        void Calculate(DroneComponent &drone, const FormationContext &ctx) override
        {
            using namespace DirectX::SimpleMath;

            // 全体の何番目か → 角度に変換。
            const int   total     = (drone.totalDrones > 0) ? drone.totalDrones : 1;
            const float baseAngle = (2.0f * 3.14159265f) *
                                    (static_cast<float>(drone.localIndex) / static_cast<float>(total));

            const float angle = baseAngle + ctx.time * 0.5f;

            const float r = drone.orbitRadius;
            Vector3     offset(std::cos(angle) * r, 0.0f, std::sin(angle) * r);
            offset.y = 2.0f; // ボスより少し上に浮かせる

            drone.targetPosition = ctx.bossPos + offset;

            Vector3 look = offset;
            look.Normalize();
            drone.targetLookDirection = look;

            // 周回は中立状態。攻撃で使った一時的な値をここで戻す
            // （BT が攻撃の合間に必ず挟むので、次の攻撃へ前回の値が漏れない）。
            drone.attackProgress = 0.0f;
            drone.fireDelay      = 0.0f;
            drone.chargeMode     = ChargeMode::Straight;
        }
    };

    // 頭上円陣（HighOrbit）: ボスの頭上高くで周回する中立陣形。攻撃と攻撃の「あいだ」は
    // こちらを使う。OrbitCircle はボスの高さで回るため、プレイヤーが接近するとドローンが
    // 壁になって地上の間合いが塞がれる。
    struct HighOrbitFormation : public IFormation
    {
        // 攻撃と攻撃の「あいだ」の中立。技ではないので震えない。
        bool UsesShiverTelegraph() const override { return false; }

        // 攻撃の形ではなくボスの頭上を回るだけなので、跳躍・滞空でもボスと一緒に上下する。
        bool FollowsBossHeight() const override { return true; }

        void Calculate(DroneComponent &drone, const FormationContext &ctx) override
        {
            using namespace DirectX::SimpleMath;

            const Vector3 offset =
                HighRing::SlotOffset(drone.localIndex, drone.totalDrones, ctx.time);

            drone.targetPosition = ctx.bossPos + offset;

            Vector3 look = offset;
            look.y       = 0.0f;
            if (look.LengthSquared() > 0.001f)
            {
                look.Normalize();
                drone.targetLookDirection = look;
            }

            // OrbitCircle と同じく、攻撃で使った一時的な値を戻す。
            drone.attackProgress = 0.0f;
            drone.fireDelay      = 0.0f;
            drone.chargeMode     = ChargeMode::Straight;
        }

        // 調整値は HighRing が持つ（弾幕と共有しているため、ここには置かない）。
    };

    inline void RegisterAll()
    {
        auto &reg = FormationRegistry::Instance();
        reg.Register<OrbitCircleFormation>(DroneFormationType::OrbitCircle);
        reg.Register<HighOrbitFormation>(DroneFormationType::HighOrbit);
    }

} // namespace CCL::AI::Formation::Basic
