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
//   ボス頭上に「縦の刃（壁）」を立て、下端を回転軸に、橋が倒れるように
//   プレイヤー側へ振り下ろす。要求回避＝横回避（企画書①）。
//
// 動きの核心＝ヒンジ回転（剛体）:
//   各機の "刃ローカル座標" は固定（下端からの高さ height と、幅方向 side）。
//   刃全体を下端の軸まわりに角度 θ 回転させる。
//     θ=0   : 直立（up 方向）        ← 予備動作（読ませる形）
//     θ=90° : 前方へ水平（dir 方向）  ← 倒れきった状態
//   回転後の高さベクトル = up*height*cos(θ) + dir*height*sin(θ)
//   → 下端(height=0)は θ に依らず不動＝軸。上段ほど大きく前へ倒れる。
//
//   θ は attackProgress(0→1) から求める。倒れは重力的に加速させたいので easeIn。
//   移動は chargeFollowsTarget=true で「targetPosition を剛体追従」させる
//   （fireDirection 直進ではなく、壁が形を保ったまま回る）。
// ====================================================================
struct GuillotineSlamFormation : public IFormation
{
    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        // この攻撃は「陣形自体が動く」ので、突撃中も targetPosition を更新し続ける。
        // 移動側にはそれを剛体追従させる（直進ではなく回転）。
        drone.chargeFollowsTarget = true;

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

        const float spacing      = 2.0f; // 刃の目の細かさ
        const float hingeHeight  = 2.0f; // 回転軸（下端）の高さ
        const float hingeForward = 5.0f; // ボスからプレイヤー側へ離す距離（押し出し回避＋間合い）

        // --- 攻撃の進行度 θ を更新する ---
        //   wantsAttack 中だけ進め、非攻撃時は 0 に戻して直立を保つ。
        if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt * kSwingSpeed);
        else
            drone.attackProgress = 0.0f;

        const float eased = drone.attackProgress * drone.attackProgress; // easeIn（倒れが加速）
        const float theta = eased * kThetaMax;                            // 0 → kThetaMax
        const float c = std::cos(theta);
        const float s = std::sin(theta);

        // --- 刃ローカル：下端からの高さ height と、幅方向オフセット side ---
        const float height = row * spacing;                       // 下端(row0)=0＝軸
        const float side   = (col - (cols - 1) * 0.5f) * spacing; // 幅方向は中央揃え

        // 回転軸（下端の中心）：ボス頭上のやや低い位置
        const Vector3 hingeBase = ctx.bossPos + Vector3::UnitY * hingeHeight;

        // 高さベクトルを下端軸まわりに θ 回転（up → dir へ倒す）
        const Vector3 rotatedUp = Vector3::UnitY * (height * c) + dir * (height * s);

        drone.targetPosition = hingeBase + right * side + rotatedUp;

        // 向き：倒れていく接線方向（-up*sin + dir*cos）を向かせる
        Vector3 look = Vector3::UnitY * (-s) + dir * c;
        if (look.LengthSquared() < 0.001f) look = dir;
        look.Normalize();
        drone.fireDirection       = look;
        drone.targetLookDirection = look;
    }

    // --- 調整値（グレーボックスで実測して詰める）---
    static constexpr float kSwingSpeed = 2.5f;               // attackProgress の増加速度（1/秒）→ 倒れ切りまで ≈0.4s
    static constexpr float kThetaMax   = 1.9f;               // 最大回転角（rad）≈109°（水平を少し越えて叩きつける）
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
