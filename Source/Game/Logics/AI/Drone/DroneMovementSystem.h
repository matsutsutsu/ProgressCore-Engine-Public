// 振付師が決定した目標座標へ向かってドローンを物理的に移動・回転させるシステム。
//
// 状態は「基礎ループ3つ」＋「個体が自分で入る2つ」で構成する。
//   Idle / MoveToTarget / FireCharge … 陣形を組む→隊列を直す→突撃する（ボスの指示で動く）
//   Stunned / Returning              … 吹き飛ばされた／飛び切った（個体が自分で入る）
//   LockOn・Hold・HorizontalCharge・Shaking は演出の上乗せなので作らない。
//   状態を増やすほど 1000機ぶんの分岐が増えるため、増やすのは
//   「見た目の動きが根本的に違うもの」だけに絞り、「1ステート=1関数」の形は崩さない。
//
// 補間係数を必ず (std::min)(k * dt, 1.0f) で止める理由:
//   Lerp / Slerp は係数が 1 を超えると補間ではなく外挿になり、目標を通り越す。
//   dt は固定ステップ（FIXED_DT）なので通常は 1 を超えないが、TimeContext の
//   baseGlobalScale は「ゲーム全体の速度調整」用に 1 より大きくできる。
//   そこを上げた瞬間に旋回だけが破綻するのは追いにくいので、係数側で止めておく。
//
// 実行順序: L03_PrePhysics（PushSystem より前）で trans を確定させる。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"

class DroneMovementSystem : public CCL::ECS::IfSystem<
                                DroneMovementSystem,
                                CCL::ECS::Write<TransformComponent>,
                                CCL::ECS::Write<DroneComponent>,
                                CCL::ECS::Read<TimeStateComponent>>
{
  public:
    DroneMovementSystem()
        : IfSystem("DroneMovementSystem")
    {
    }
    virtual ~DroneMovementSystem() override = default;

    virtual void Update(float dt) override;

  private:
    // =========================================================
    // 各関数は drone.currentVelocity（意思）と rotation を計算する。
    // 最後に trans.position へ積分するのは Update 側の共通処理が担う。
    // =========================================================

    // 滑らかに目標へ移動・回転する (Lerp/Slerp)。陣形の再構築に使う。
    inline void ProcessMoveToTarget(TransformComponent &trans, DroneComponent &drone, float dt);

    // 陣形を維持しつつフワフワ追従する。待機・巡回に使う。
    inline void ProcessIdle(TransformComponent &trans, DroneComponent &drone, float dt);

    // 目標方向へ直線的に高速突撃する。ジャスト回避の対象となる攻撃状態。
    inline void ProcessFireCharge(TransformComponent &trans, DroneComponent &drone, float dt);

    // 隊列を組んだままの突入。targetPosition へバネで追従する。
    //
    // 剛体追従と違って形が歪む。歪むことがこの技の見た目の核。
    inline void ProcessChargeSoftFollow(TransformComponent &trans, DroneComponent &drone, float dt);

    // 切り払いで吹き飛ばされ、制御を失って漂う。通常の追従・突撃を一切行わない。
    //
    // タイマーの管理は DroneBrainSystem 側。ここは「漂って減衰する」動きだけを持つ。
    inline void ProcessStunned(TransformComponent &trans, DroneComponent &drone, float dt);

    // 突き抜けた後、上方へ抜けながら減速する離脱カーブ。
    //
    // その場で急旋回させると突撃の勢いが嘘になるため、一度抜けさせてから
    // MoveToTarget に隊列復帰を任せる。
    inline void ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt);

    // --- 陣形へ戻る（MoveToTarget）---
    //   持ち場のズレに掛けるバネの強さと、その速度追従の滑らかさ。
    //   Idle より強く取ってあるのは、隊列を「直す」局面だからで、
    //   同じ値にすると崩れた隊形が整うまでに時間がかかりすぎる。
    static constexpr float kMoveToTargetGain    = 20.0f;
    static constexpr float kMoveToTargetDamping = 15.0f;
    static constexpr float kMoveToTargetSpeedScale = 2.5f;  // moveSpeed に掛ける上限

    // --- 陣形を保つ（Idle）---
    //   こちらは「維持」なのでバネを弱くする。強いと目標に貼り付いてしまい、
    //   ホバーのゆらぎが打ち消されて機械的に見える。
    static constexpr float kIdleGain        = 10.0f;
    static constexpr float kIdleDamping     =  8.0f;
    static constexpr float kIdleSpeedScale  =  1.5f;

    // ホバーのゆらぎ。位相は個体ごとに散らしてある（DroneComponent の hoverTimeOffset）。
    static constexpr float kHoverFrequency = 1.0f;   // ※ kHoverPhasePeriod の巻き戻しが成立するよう整数にする
    static constexpr float kHoverAmplitude = 3.0f;

    // --- 突撃のタメ（予備動作）---
    //   後ろへ引き絞りながら持ち上がり、細かく震える。ジャスト回避を「読ませる」時間帯。
    static constexpr float kWindupDrawback = 2.0f;   // fireDirection と逆向きに引く速さ
    static constexpr float kWindupLift     = 4.0f;   // 同時に持ち上げる速さ
    static constexpr float kWindupJitter   = 5.5f;   // 震えの振幅 [m/s]
    //   震えの角速度。3軸で少しずつ変えて、単純な往復に見えないようにする。
    //   kHoverPhasePeriod(2π) で位相を巻き戻すので、すべて整数のままにすること。
    static constexpr float kWindupJitterSpeedX = 100.0f;
    static constexpr float kWindupJitterSpeedY = 120.0f;
    static constexpr float kWindupJitterSpeedZ =  80.0f;

    // 突撃（リリース）の速度倍率。moveSpeed に掛ける。
    static constexpr float kChargeSpeedScale = 2.5f;

    // --- 姿勢の追従速度（1/秒）。局面ごとに変える ---
    //   整列中は機敏に、待機はゆるく、突撃は最も速く、離脱は最も鈍く。
    //   離脱だけ遅いのは「制御を取り戻しつつある」表現。
    static constexpr float kTurnMoveToTarget = 25.0f;
    static constexpr float kTurnIdle         = 15.0f;
    static constexpr float kTurnFireDelay    = 15.0f;
    static constexpr float kTurnWindup       = 20.0f;
    static constexpr float kTurnCharge       = 25.0f;
    static constexpr float kTurnSoftFollow   = 12.0f;
    static constexpr float kTurnReturning    =  8.0f;

    // 吹き飛ばし中の回転速度（rad/秒）。姿勢制御を失っている表現。
    static constexpr float kStunTumbleRate = 4.0f;

    // 吹き飛ばし速度の減衰率（1/秒）。数フレームで初速→ほぼ0にする。
    static constexpr float kKnockbackDecay = 6.0f;

    // 離脱中に上方向へ加える加速度。突撃の軌道から「抜ける」ための持ち上げ。
    static constexpr float kBreakawayLift = 18.0f;

    // 離脱中の減速率（1/秒）。突撃速度を帰投できる速さまで落とす。
    static constexpr float kBreakawayDrag = 2.5f;

    // 同じく速度の追従の滑らかさ。
    static constexpr float kSoftFollowDamping = 10.0f;

    // 隊列追従時の最高速度倍率。頭の突入速度を上回る必要がある。
    static constexpr float kSoftFollowMaxSpeedScale = 2.4f;

    // slotInSquad から追従の鈍さ係数を出す。
    //
    // 後方の機ほどワンテンポ遅れて曲がるので、隊列がムチのようにしなる。
    // 「編隊で飛んでいる」感の大半はこの遅れから出ている。
    // 小隊に属さない機は slotInSquad=0 なので影響を受けない。
    float SlotLag(const DroneComponent &drone) const
    {
        const float lag = 1.0f - static_cast<float>(drone.slotInSquad) * m_slotLagPerSlot;
        return (lag < 0.15f) ? 0.15f : lag;   // 遅すぎて隊列から千切れないよう下限を置く
    }

    // --- SquadTuning から毎フレーム写し取る（ImGui で実行中に変えられる）---
    //   並列ループから resource を引かずに済むよう、Update の先頭で1回だけコピーする。
    float m_followGain     = 10.0f;
    float m_slotLagPerSlot = 0.06f;
};