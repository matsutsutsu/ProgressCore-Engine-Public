// 基本的な陣形クラスをまとめて定義（役割ごとに1ファイル）。
#pragma once
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include <cmath>

namespace CCL::AI::Formation::Basic
{
// ====================================================================
// 頭上の待機リング。頭上円陣(HighOrbit)と弾幕(KamikazeSwarm)が共有する。
//
// なぜ共有するか:
//   BT は必ず「頭上円陣 →（技）→ 頭上円陣」の順で指示する。弾幕もその輪の
//   まま撃たせたい。半径と高さを技側へコピーすると、いつか片方だけ直って
//   切り替えの瞬間に群れが横滑りする。出どころを1つにしておく。
//
// 回転位相まで揃えることに意味がある:
//   半径と高さだけ合わせても ctx.time の項が違えば輪は別の位置にあり、
//   陣形を切り替えた瞬間に全機が並び直してしまう。同じ式を通していれば
//   切り替えでドローンは1ミリも動かず、「上空の形のまま」が成立する。
// ====================================================================
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
        const float angle = baseAngle + time * kSpin;

        return {std::cos(angle) * kRadius, kHeight, std::sin(angle) * kRadius};
    }
};

// ====================================================================
// 円陣（OrbitCircle）: ボスを中心に円を描いて並ぶ
//   localIndex / totalDrones から自分の角度を決める。
//   time を足すとゆっくり回転する。
// ====================================================================
struct OrbitCircleFormation : public IFormation
{
    // 隊形（位置取り）であって技ではないので、予告の震えを出さない。
    bool UsesShiverTelegraph() const override { return false; }

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        // 全体の何番目か → 角度に変換（360度を totalDrones で分割）
        const int   total     = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const float baseAngle = (2.0f * 3.14159265f) *
                                (static_cast<float>(drone.localIndex) / static_cast<float>(total));

        // 時間で回転させる（ゆっくり周回）
        const float angle = baseAngle + ctx.time * 0.5f;

        // 円周上の座標（ボス中心）
        const float r = drone.orbitRadius;
        Vector3     offset(std::cos(angle) * r, 0.0f, std::sin(angle) * r);
        offset.y = 2.0f; // ボスより少し上に浮かせる

        drone.targetPosition = ctx.bossPos + offset;

        // 視線はボスの外側（プレイヤー方向でも可）
        Vector3 look = offset;
        look.Normalize();
        drone.targetLookDirection = look;

        // 陣形維持中は「有機的追従」ステートが自然
        //drone.currentState = DroneState::Idle;

        // 周回＝中立状態。攻撃で使ったスクラッチをここでリセットする
        // （BT が攻撃と攻撃の間に必ず OrbitCircle を挟むので、次の攻撃へ前回の値が漏れない）。
        drone.attackProgress      = 0.0f;
        drone.fireDelay           = 0.0f;
        drone.chargeMode = ChargeMode::Straight;
    }
};

// ====================================================================
// 頭上円陣（HighOrbit）: ボスの「頭上高く」で円を描いて周回する中立陣形。
//   OrbitCircle はボスの高さ(≈2m)で回るため、プレイヤーが接近するとドローンが
//   壁になって近づけない（＝意図しない接近拒否）。攻撃していない間はドローンを
//   頭上へ退避させ、地上の間合い（プレイヤー↔ボス）を空ける。
//   → 攻撃と攻撃の「あいだ」の中立はこちらを使う（BT が Orbit の代わりに指定）。
// ====================================================================
struct HighOrbitFormation : public IFormation
{
    // 攻撃と攻撃の「あいだ」の中立。技ではないので震えない。
    bool UsesShiverTelegraph() const override { return false; }

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        const Vector3 offset =
            HighRing::SlotOffset(drone.localIndex, drone.totalDrones, ctx.time);

        drone.targetPosition = ctx.bossPos + offset;

        // 視線は外向き（水平）
        Vector3 look = offset;
        look.y = 0.0f;
        if (look.LengthSquared() > 0.001f) { look.Normalize(); drone.targetLookDirection = look; }

        // 中立状態＝攻撃で使ったスクラッチをリセット（次の攻撃へ前回値を漏らさない）。
        // OrbitCircle と同じ役割。BT が攻撃の合間にこの陣形を挟む前提。
        drone.attackProgress      = 0.0f;
        drone.fireDelay           = 0.0f;
        drone.chargeMode = ChargeMode::Straight;
    }

    // 調整値は HighRing が持つ（弾幕と共有しているため、ここには置かない）。
};

// … 陣形を1つ足すたびに1クラスずつここへ追加する …

// ====================================================================
// 登録
// ====================================================================
inline void RegisterAll()
{
    auto &reg = FormationRegistry::Instance();
    reg.Register<OrbitCircleFormation>(DroneFormationType::OrbitCircle);
    reg.Register<HighOrbitFormation>(DroneFormationType::HighOrbit);
    // … 追加した分だけ登録 …
}

} // namespace CCL::AI::Formation::Basic
