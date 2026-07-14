/// @file  Formations_Attack.h
/// @brief 攻撃系の陣形クラス（予備動作フェーズで「形」を作り、fireDirection を仕込む）。
///        Formations_Basic.h（周回・待機系）と対に、攻撃技をここへ1クラスずつ追加する。
///
/// 攻撃陣形の責務:
///   - 予備動作フェーズ（currentState != FireCharge）中に targetPosition で「形」を作る。
///     この形をプレイヤーが読む（企画書の「形を読む」コア体験）。
///   - 同時に fireDirection（突撃方向）を仕込んでおく。FireCharge へ移った瞬間、
///     DroneMovementSystem が fireDirection に沿って「タメ→リリース」を行う。
///   - FireCharge 中は座標・方向を凍結し、コミットした直進を保証する
///     （ホーミングさせない＝ジャスト回避で抜けられる）。
#pragma once
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include <cmath>
#include <algorithm>

namespace CCL::AI::Formation::Attack
{
// ====================================================================
// 縦割り（GuillotineSlam）
//   ボスの頭上・ややプレイヤー寄りに「縦の刃（壁）」を形成し、
//   プレイヤーへ向けて振り下ろす。要求回避＝横回避（企画書①）。
// ====================================================================
struct GuillotineSlamFormation : public IFormation
{
    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        // 突撃中は座標・方向を凍結（リリース後にホーミングさせない）。
        if (drone.currentState == DroneState::FireCharge)
            return;

        // --- ボス→プレイヤーの水平方向と、それに直交する「刃の幅」方向 ---
        Vector3 dir = ctx.playerPos - ctx.bossPos;
        dir.y = 0.0f;
        if (dir.LengthSquared() < 0.001f) dir = Vector3::UnitZ;
        dir.Normalize();

        Vector3 right = Vector3::UnitY.Cross(dir);
        if (right.LengthSquared() < 0.001f) right = Vector3::UnitX;
        right.Normalize();

        // --- localIndex を (col, row) の格子へ割り付けて「壁」を作る ---
        const int total = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const int cols  = (std::max)(1, static_cast<int>(std::floor(std::sqrt(static_cast<float>(total)))));
        const int col   = drone.localIndex % cols;
        const int row   = drone.localIndex / cols;

        const float spacing       = 2.0f; // 刃の目の細かさ
        const float baseHeight    = 9.0f; // ボス頭上の高さ
        const float forwardOffset = 4.0f; // プレイヤー側への寄せ

        // 刃の中心（ボス頭上・ややプレイヤー寄り）
        const Vector3 bladeCenter = ctx.bossPos + dir * forwardOffset + Vector3::UnitY * baseHeight;

        // 幅方向は中央揃え、高さ方向は上へ積む
        const Vector3 offset = right * ((col - (cols - 1) * 0.5f) * spacing)
                             + Vector3::UnitY * (row * spacing);

        drone.targetPosition = bladeCenter + offset;

        // 刃が「振り下ろされる」方向＝各機からプレイヤーへ（下＋前の成分を持つ）
        Vector3 toPlayer = ctx.playerPos - drone.targetPosition;
        if (toPlayer.LengthSquared() > 0.001f) toPlayer.Normalize();
        else                                   toPlayer = -Vector3::UnitY;

        drone.fireDirection       = toPlayer;
        drone.targetLookDirection = toPlayer;
    }
};

// ====================================================================
// 登録（DroneFormationSystem::Initialize から呼ぶ）
// ====================================================================
inline void RegisterAll()
{
    auto &reg = FormationRegistry::Instance();
    reg.Register<GuillotineSlamFormation>(DroneFormationType::GuillotineSlam);
    // … 今後 ClapSlam（挟み込み）/ 弾幕 などをここへ1クラスずつ追加 …
}

} // namespace CCL::AI::Formation::Attack
