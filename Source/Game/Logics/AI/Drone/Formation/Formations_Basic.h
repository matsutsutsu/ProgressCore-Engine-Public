/// @file  Formations_Basic.h
/// @brief 基本的な陣形クラスをまとめて定義（役割ごとに1ファイル）。
///        旧 DroneFormationSystem の CalculateXxx 群を IFormation に移植していく。
///        まずは動作確認用に OrbitCircle（円陣）だけ実装。
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
    }
};

// … 今後 DeathRing / AegisShield / AllCharge などをここに1クラスずつ追加 …

// ====================================================================
// 登録
// ====================================================================
inline void RegisterAll()
{
    auto &reg = FormationRegistry::Instance();
    reg.Register<OrbitCircleFormation>(DroneFormationType::OrbitCircle);
    // reg.Register<DeathRingFormation>(DroneFormationType::DeathRing);
    // … 追加した分だけ登録 …
}

} // namespace CCL::AI::Formation::Basic