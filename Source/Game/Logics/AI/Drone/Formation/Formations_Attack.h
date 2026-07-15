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
// 挟み込み1段（ClapSlam）
//   ボス前方の左右に「低い壁」を2枚作り、左右同時に中央へスライドして
//   プレイヤーを挟み込む。壁は低いのでジャンプで越えて避ける。要求回避＝ジャンプ（企画書②）。
//
// 動きの核心＝左右の壁が中央へ寄る（剛体スライド）:
//   localIndex の前半＝左の壁、後半＝右の壁。各壁は「奥行き(dir)×高さ(up)」の格子。
//   高さは低く抑える（＝ジャンプで越えられる）。
//   進行度 attackProgress(0→1) で左右の間隔 gap を 広→狭 に縮める＝バチンと挟む。
//   縦割りと同じく chargeFollowsTarget=true で targetPosition を剛体追従させる。
// ====================================================================
struct ClapSlamFormation : public IFormation
{
    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        drone.chargeFollowsTarget = true; // 壁が形を保ったままスライドする

        Vector3 dir = ctx.playerPos - ctx.bossPos;
        dir.y = 0.0f;
        if (dir.LengthSquared() < 0.001f) dir = Vector3::UnitZ;
        dir.Normalize();

        Vector3 right = Vector3::UnitY.Cross(dir);
        if (right.LengthSquared() < 0.001f) right = Vector3::UnitX;
        right.Normalize();

        // --- 左右どちらの壁か・その壁の中での通し番号 k ---
        const int total  = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const int half   = (std::max)(1, total / 2);
        const bool isLeft = (drone.localIndex < static_cast<uint16_t>(half));
        const int k      = isLeft ? drone.localIndex : (drone.localIndex - half);

        // --- 壁の格子：低い高さ(row) × 奥行き(col) ---
        const int   heightRows    = 2;    // 低い壁（ジャンプで越えられる）
        const int   perSide       = half;
        const int   colsPerSide   = (std::max)(1, (perSide + heightRows - 1) / heightRows);
        const int   col           = k / heightRows; // 奥行き方向
        const int   row           = k % heightRows; // 高さ方向

        const float depthSpacing  = 2.5f; // 壁の奥行きの間隔
        const float heightSpacing = 1.5f; // 高さの間隔（低め）
        const float baseHeight    = 0.8f; // 壁の下端の高さ（地面付近）
        const float clapForward   = 6.0f; // ボスからプレイヤー側への壁の中心距離

        // --- 進行度で左右間隔 gap を縮める（挟み込み）---
        if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt * kClapSpeed);
        else
            drone.attackProgress = 0.0f;

        const float eased = drone.attackProgress * drone.attackProgress;  // easeIn（バチンと閉じる）
        const float gap   = kStartGap + (kEndGap - kStartGap) * eased;    // 広 → 狭

        const float sideSign = isLeft ? -1.0f : 1.0f;
        const float depth    = (col - (colsPerSide - 1) * 0.5f) * depthSpacing; // 奥行きは中央揃え

        const Vector3 center = ctx.bossPos + dir * clapForward + Vector3::UnitY * baseHeight;
        drone.targetPosition = center
                             + right * (sideSign * gap)          // 左右から寄る
                             + dir   * depth                     // 奥行きに広がる壁
                             + Vector3::UnitY * (row * heightSpacing);

        // 向き：相手側（内向き）を向かせる
        Vector3 look = right * (-sideSign);
        if (look.LengthSquared() < 0.001f) look = dir;
        look.Normalize();
        drone.fireDirection       = look;
        drone.targetLookDirection = look;
    }

    // --- 調整値（グレーボックスで実測して詰める）---
    static constexpr float kClapSpeed = 3.0f;   // attackProgress の増加速度 → 閉じ切りまで ≈0.33s
    static constexpr float kStartGap  = 12.0f;  // 開始時の左右間隔（広く開いて読ませる）
    static constexpr float kEndGap    = 1.2f;   // 閉じ切り時の左右間隔（ほぼ接触）
};

// ====================================================================
// 弾幕（KamikazeSwarm）※初めての dashモード攻撃
//   ボス頭上にドローンを集め、プレイヤーへ向かって「順番に」高速で撃ち出す。
//   プレイヤーはジャスト回避で凌ぐ（企画書⑥）。
//
// 縦割り／挟み込みとの違い＝移動モードが逆:
//   これらは chargeFollowsTarget=false のまま＝各機が fireDirection へ独立に直進する
//   「弾（fire-and-forget）」。DroneMovementSystem の「タメ→リリース」がそのまま弾になる。
//
// 「順番に」の作り方:
//   forming 中に localIndex から fireDelay を割り振る（先頭ほど早く撃つ）。
//   全機が同時に FireCharge へ入っても、ProcessFireCharge が fireDelay の間その場で
//   待機させるため、結果として順次発射になる。
//   fireDelay = (localIndex / total) * kFireWindow で、機数に依らず常に kFireWindow 秒で
//   撃ち切る（1000機でも一定時間で発射）。
// ====================================================================
struct KamikazeSwarmFormation : public IFormation
{
    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        // 発射後は各機が独立して飛ぶので、撃ち出したら陣形は関与しない（凍結）。
        if (drone.currentState == DroneState::FireCharge)
            return;

        drone.chargeFollowsTarget = false; // ★dashモード（fireDirection直進）

        Vector3 dir = ctx.playerPos - ctx.bossPos;
        dir.y = 0.0f;
        if (dir.LengthSquared() < 0.001f) dir = Vector3::UnitZ;
        dir.Normalize();

        // --- forming：ボス頭上に「弾の供給リング」を作って待機させる ---
        const int   total = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const float angle = (2.0f * 3.14159265f) * (static_cast<float>(drone.localIndex) / static_cast<float>(total));
        const float ringR = 3.5f;
        const float ringH = 7.0f; // ボス頭上

        Vector3 pos = ctx.bossPos + Vector3::UnitY * ringH
                    + Vector3(std::cos(angle) * ringR, 0.0f, std::sin(angle) * ringR);
        drone.targetPosition = pos;

        // 発射方向：待機位置からプレイヤーへ（発射時にこの方向でロックされる）
        Vector3 toPlayer = ctx.playerPos - pos;
        if (toPlayer.LengthSquared() > 0.001f) toPlayer.Normalize();
        else                                   toPlayer = dir;
        drone.fireDirection       = toPlayer;
        drone.targetLookDirection = toPlayer;

        // 発射順（先頭ほど早い）。機数に依らず kFireWindow 秒で撃ち切る。
        drone.fireDelay = (static_cast<float>(drone.localIndex) / static_cast<float>(total)) * kFireWindow;
    }

    // --- 調整値（グレーボックスで実測して詰める）---
    static constexpr float kFireWindow = 1.8f; // 全機を撃ち切るのにかける秒数（順次発射の総時間）
};

// ====================================================================
// 登録（DroneFormationSystem::Initialize から呼ぶ）
// ====================================================================
inline void RegisterAll()
{
    auto &reg = FormationRegistry::Instance();
    reg.Register<GuillotineSlamFormation>(DroneFormationType::GuillotineSlam);
    reg.Register<ClapSlamFormation>(DroneFormationType::ClapSlam);
    reg.Register<KamikazeSwarmFormation>(DroneFormationType::KamikazeSwarm);
}

} // namespace CCL::AI::Formation::Attack
