// 目標座標へドローンを物理的に移動・回転させるシステム。実行順序は L03_PrePhysics。
//
// 状態はボスの指示で動く3つ（Idle / MoveToTarget / FireCharge）と、個体が自分で
// 入る2つ（Stunned / Returning）。演出は上乗せで表現し、ステートにはしない。
//
// 補間係数は必ず (std::min)(k * dt, 1.0f) で止める。1 を超えると外挿になって
// 目標を通り越す（globalScale を 1 より大きくできるので dt 側では保証されない）。
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
    // 各関数は drone.currentVelocity と rotation だけを計算する。
    // trans.position への積分は Update 側の共通処理が担う。

    inline void ProcessMoveToTarget(TransformComponent &trans, DroneComponent &drone, float dt);
    inline void ProcessIdle(TransformComponent &trans, DroneComponent &drone, float dt);
    inline void ProcessFireCharge(TransformComponent &trans, DroneComponent &drone, float dt);

    // 隊列を組んだままの突入。剛体追従と違って形が歪む。歪むのがこの技の見た目の核。
    inline void ProcessChargeSoftFollow(TransformComponent &trans, DroneComponent &drone, float dt);

    // 進行方向 → 姿勢。yaw の式をここ1箇所に閉じ込める。
    //
    //   ドローンのモデルは前方が -Z。CreateFromYawPitchRoll は行ベクトルで
    //   v·Rx(pitch)·Ry(yaw) の順に掛かるので、
    //     (0,0,-1)·Rx(p) = (0, +sin p, -cos p)        ← pitch = asin(look.y) と一致する
    //     ここへ Ry(θ) = (-cos p·sin θ, sin p, -cos p·cos θ)
    //   となり、look と突き合わせると θ = atan2(-x, -z)。
    //   +Z 前提の atan2(x, z) や、以前ここにあった atan2(x, -z) と混ぜると
    //   Z軸に対する鏡像になり、状態が切り替わるたびに機首が最大180°反転する
    //   （±Z を向いているときだけ一致するので気づきにくい）。
    //
    //   includePitch=false は Idle のように「水平だけ向かせたい」場面用。
    //   includePitch=true のときだけ look の正規化が要る（asin に渡すため）。
    DirectX::SimpleMath::Quaternion LookToRotation(const DirectX::SimpleMath::Vector3 &look,
                                                   bool includePitch) const;

    // 予告の震えの位置オフセット [m]。t は構え始めてからの経過秒。
    //   t を取るのは前フレームぶんを引いて差分だけ動かすため（足しっぱなしにすると
    //   座標へ溜まって群れごと流れていく）。
    DirectX::SimpleMath::Vector3 ShiverOffset(const DroneComponent &drone, float t,
                                              float amplitude) const;

    // 吹き飛ばされて漂う。タイマー管理は DroneBrainSystem 側で、ここは動きだけ。
    inline void ProcessStunned(TransformComponent &trans, DroneComponent &drone, float dt);

    // 突き抜けた後、上方へ抜けながら減速する離脱カーブ。その場で急旋回させると
    // 突撃の勢いが嘘になるので、一度抜けさせてから MoveToTarget に復帰を任せる。
    inline void ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt);

    // 陣形へ戻る（MoveToTarget）。隊列を「直す」局面なので Idle より強い。
    static constexpr float kMoveToTargetGain    = 20.0f;
    static constexpr float kMoveToTargetDamping = 15.0f;
    static constexpr float kMoveToTargetSpeedScale = 2.5f;

    // 陣形を保つ（Idle）。強くすると目標に貼り付いてホバーのゆらぎが消える。
    static constexpr float kIdleGain        = 10.0f;
    static constexpr float kIdleDamping     =  8.0f;
    static constexpr float kIdleSpeedScale  =  1.5f;

    static constexpr float kHoverFrequency = 1.0f;   // kHoverPhasePeriod の巻き戻しが成立するよう整数にする
    static constexpr float kHoverAmplitude = 3.0f;

    // 突撃のタメ。「静止 → 加速」の2段でジャスト回避を読ませる。
    //
    //     |<------ DroneBrainSystem::kChargeWindup (0.4秒) ------>|
    //     |<-- 静止 (0.2秒) -->|<-- kWindupLungeTime (0.2秒) -->| 射出
    //
    //   静止の長さは引き算で決まる。中央で一度止まることで「集まった」と「撃った」が
    //   別々の出来事として読める。定速にすると速度の段差が2回できてカクつく。
    static constexpr float kWindupLungeTime  = 0.2f;  // 加速にかける長さ [秒]
    static constexpr float kWindupLungeSpeed = 20.0f; // 加速の終速 [m/s]

    // 陣形攻撃の予告（震え）。縦割りのように予備動作でほとんど動かない技は
    // 動きで予告できないので、合図の前に震えを出す。弾幕はこれを使わない
    // （動きで予告する技に足すと予告が二重になって読めなくなる）。
    //   単位は m/s ではなく m。速度で揺らすと実際の幅が「振幅 ÷ 角速度」になる。
    //   震え方は目で見ないと決まらないので定数にせず World リソースが持つ。
    DroneShiverTuning m_shiver{};

    // 位相を機体ごとにずらす幅 [rad]。黄金角なので何機いても一様にばらける。
    static constexpr float kShiverPhaseStep = 2.39996323f;

    // 突撃の速度倍率。実体が DroneComponent.h にあるのは弾幕の陣形も同じ値で
    // 「飛翔時間 = 距離 ÷ 射出速度」を出すため。
    //   上げすぎると擦り抜ける。1フレームの移動距離が当たり半径の2倍を超えると
    //   跨いで通り過ぎる（半径2.5m・60fps なら 300 m/s が上限の目安）。
    static constexpr float kChargeSpeedScale = kDroneChargeSpeedScale;

    // 剛体追従（縦割り・挟み込み等）の終わり際に残す惰性。
    //   形が動いている間の速度をこの割合だけ currentVelocity へ残し、動き切ったあとは
    //   Idle のバネへ渡す。ここが 0 だと刃や腕が閉じ切った瞬間に全機が空中で同時停止し、
    //   「ピシッと止まる」形で不自然に見える。
    //   剛体そのものは壊れない（残した速度ぶんを座標から引いてから積分へ渡すので、
    //   倍率をいくつにしても着地点は targetPosition ちょうどになる）。
    static constexpr float kRigidFollowInertia = 0.35f;

    // 上で残す速度の上限（moveSpeed 倍）。腕の外側の機は回転半径ぶん速度が跳ね上がる
    // ので、そのまま残すと収まらずに飛んでいく。
    static constexpr float kRigidFollowInertiaMaxScale = 0.6f;

    // 姿勢の追従速度（1/秒）。離脱だけ鈍いのは「制御を取り戻しつつある」表現。
    static constexpr float kTurnMoveToTarget = 25.0f;
    static constexpr float kTurnIdle         = 15.0f;
    static constexpr float kTurnFireDelay    = 15.0f;
    static constexpr float kTurnWindup       = 20.0f;
    static constexpr float kTurnCharge       = 25.0f;
    static constexpr float kTurnSoftFollow   = 12.0f;
    static constexpr float kTurnReturning    =  8.0f;

    static constexpr float kStunTumbleRate = 4.0f;  // 吹き飛ばし中の回転 [rad/s]
    static constexpr float kKnockbackDecay = 6.0f;  // 吹き飛ばし速度の減衰率 [1/s]
    static constexpr float kBreakawayLift  = 18.0f; // 離脱時に上方向へ加える加速度

    // 浮き上がりを効かせ始める速さ [m/s]。全速のまま上向きの力を加えると、
    // 突き抜けた瞬間に軌道が跳ね上がる。
    static constexpr float kBreakawayFastSpeed = 40.0f;

    static constexpr float kBreakawayDrag = 2.5f;   // 離脱中の減速率 [1/s]

    // 隊列追従の滑らかさと最高速度倍率（頭の突入速度を上回る必要がある）。
    static constexpr float kSoftFollowDamping = 10.0f;
    static constexpr float kSoftFollowMaxSpeedScale = 2.4f;

    // slotInSquad から追従の鈍さ係数を出す。後方の機ほどワンテンポ遅れて曲がるので
    // 隊列がムチのようにしなる。「編隊で飛んでいる」感の大半はこの遅れから出ている。
    float SlotLag(const DroneComponent &drone) const
    {
        const float lag = 1.0f - static_cast<float>(drone.slotInSquad) * m_slotLagPerSlot;
        return (lag < 0.15f) ? 0.15f : lag;   // 遅すぎて隊列から千切れないよう下限を置く
    }

    // SquadTuning から Update の先頭で写し取る（並列ループから resource を引かない）。
    float m_followGain     = 10.0f;
    float m_slotLagPerSlot = 0.06f;
};
