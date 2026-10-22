#pragma once
// ===========================================================================
// 概要: 攻撃しない中立の陣形（低空円陣・頭上円陣）と、頭上の待機リング。
//
// 責務:
//   - LowOrbit: ボスの周りを低い高さで周回する
//   - HighOrbit: ボスの頭上高くで周回する（攻撃と攻撃のあいだに使う）
//   - HighRing: 頭上円陣と弾幕が共有する待機リングの位置計算
//   - 攻撃で使った値（attackProgress・chargeMode）を戻す
// ===========================================================================
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include <cmath>

namespace CCL::AI::Formation::Basic
{
    /**
     * @brief 通し番号を円周上の角度にする。全機で円を等分し、時間とともに回す。低空円陣と頭上リングが使う。
     * @param localIndex  群れの中の通し番号。
     * @param totalDrones 群れの総数。0 は 1 として扱う。
     * @param time        陣形の経過時間 [秒]。
     * @param spin        回る速さ [rad/s]。
     * @return 角度 [rad]。
     */
    inline float RingAngle(uint16_t localIndex, uint16_t totalDrones, float time, float spin)
    {
        const int total = (totalDrones > 0) ? totalDrones : 1;
        return DirectX::XM_2PI * (static_cast<float>(localIndex) / static_cast<float>(total)) + time * spin;
    }

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
            const float angle = RingAngle(localIndex, totalDrones, time, kSpin);
            return {std::cos(angle) * kRadius, kHeight, std::sin(angle) * kRadius};
        }
    };

    // 低空円陣（LowOrbit）: ボスを中心に円を描いて並ぶ。
    struct LowOrbitFormation : public IFormation
    {
        // 隊形（位置取り）であって技ではないので、予告の震えを出さない。
        bool UsesShiverTelegraph() const override { return false; }

        void Calculate(DroneComponent &drone, const FormationContext &ctx) override
        {
            using namespace DirectX::SimpleMath;

            // 全体の何番目か → 角度に変換。
            const float angle = RingAngle(drone.localIndex, drone.totalDrones, ctx.time, kSpin);

            const float r = drone.orbitRadius;
            Vector3     offset(std::cos(angle) * r, kHeight, std::sin(angle) * r);

            drone.targetPosition = ctx.bossPos + offset;

            Vector3 look = offset;
            look.Normalize();
            drone.targetLookDirection = look;

            // 周回は中立状態。攻撃で使った一時的な値をここで戻す
            // （BT が攻撃の合間に必ず挟むので、次の攻撃へ前回の値が漏れない）。
            drone.attackProgress = 0.0f;
            drone.chargeMode     = ChargeMode::Straight;
        }

        static constexpr float kSpin   = 0.5f; // 回転速度 [rad/s]
        static constexpr float kHeight = 2.0f; // ボスより少し上に浮かせる高さ [m]
    };

    // 頭上円陣（HighOrbit）: ボスの頭上高くで周回する中立陣形。攻撃と攻撃の「あいだ」は
    // こちらを使う。LowOrbit はボスの高さで回るため、プレイヤーが接近するとドローンが
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

            // LowOrbit と同じく、攻撃で使った一時的な値を戻す。
            drone.attackProgress = 0.0f;
            drone.chargeMode     = ChargeMode::Straight;
        }

        // 調整値は HighRing が持つ（弾幕と共有しているため、ここには置かない）。
    };

    inline void RegisterAll(FormationRegistry &reg)
    {
        reg.Register<LowOrbitFormation>(DroneFormationType::LowOrbit);
        reg.Register<HighOrbitFormation>(DroneFormationType::HighOrbit);
    }

} // namespace CCL::AI::Formation::Basic
