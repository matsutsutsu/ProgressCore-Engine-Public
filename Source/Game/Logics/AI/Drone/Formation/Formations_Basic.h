// 基本的な陣形クラスをまとめて定義（役割ごとに1ファイル）。
#pragma once
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include <cmath>

namespace CCL::AI::Formation::Basic
{
// ====================================================================
// 円陣（OrbitCircle）: ボスを中心に円を描いて並ぶ
//   localIndex / totalDrones から自分の角度を決める。
//   time を足すとゆっくり回転する。
// ====================================================================
struct OrbitCircleFormation : public IFormation
{
    // 隊形（位置取り）であって技ではないので、予告の震えを出さない。
    bool IsTechnique() const override { return false; }

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
    bool IsTechnique() const override { return false; }

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        const int   total     = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const float baseAngle = (2.0f * 3.14159265f) *
                                (static_cast<float>(drone.localIndex) / static_cast<float>(total));
        const float angle = baseAngle + ctx.time * 0.5f;

        // --- 半径：固定値（機数に依らず一定）---
        const float r = kOrbitRadius;

        Vector3 offset(std::cos(angle) * r, 0.0f, std::sin(angle) * r);
        offset.y = kHighOrbitHeight; // ボスの頭上高く（地上の間合いを塞がない）

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

    // --- 調整値 ---
    static constexpr float kHighOrbitHeight = 20.0f; // ボス頭上の周回高さ（ボスの背丈に合わせて調整）
    static constexpr float kOrbitRadius     = 20.0f; // 円の半径（固定。詰まる/広すぎるならここを変える）
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
