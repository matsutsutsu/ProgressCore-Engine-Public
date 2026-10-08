#pragma once
// ===========================================================================
// 概要: ドローンを目標座標へ移動・回転させる System（L03_PrePhysics）。
//
// 責務:
//   - currentState ごとに速度（currentVelocity）と姿勢を計算する
//   - 速度を位置へ積分し、床より下へ潜らせない
//   - ホバーの揺れと予告の震えを位置に上乗せする
//
// 備考:
//   状態はボスの指示で動く3つ（Idle / MoveToTarget / FireCharge）と、個体が自分で
//   入る2つ（Stunned / Returning）。
//
//   ホバーの揺れや予告の震えなどの演出は上乗せで表現し、ステートにはしない。
//
//   補間係数は必ず (std::min)(k * dt, 1.0f) で止める。1 を超えると外挿になって
//   目標を通り越す（globalScale を 1 より大きくできるので dt 側では保証されない）。
//
//   状態ごとの調整値（バネの強さ・速さ・機首を向け直す速さ）は、.cpp の先頭に状態ごとにまとめてある。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Components/DroneTuning.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"

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

    void Update(float dt) override;

private:
    // ---- 状態ごとの移動 ----
    // 各関数は drone.currentVelocity と rotation だけを計算する。
    // trans.position への積分は Update 側の共通処理が担う。

    // 陣形へ戻る。隊列を「直す」局面なので、Idle より強いバネで寄せる。
    void ProcessMoveToTarget(TransformComponent &trans, DroneComponent &drone, float dt);

    // 陣形を保つ。弱いバネで寄せ、ホバーの揺れと予告の震えを足す。
    void ProcessIdle(TransformComponent &trans, DroneComponent &drone, float dt);

    /**
     * @brief 突撃。stateTimer > 0 の間が「タメ」、0 になった瞬間から「リリース」（高速直進）。
     *
     * 内部で2段階を持つので、外からは FireCharge の1状態を見るだけで予兆を表現できる。
     */
    void ProcessFireCharge(TransformComponent &trans, DroneComponent &drone, float dt);

    /**
     * @brief 隊列を組んだままの突入。剛体追従と違って形が歪む。歪むことがこの技の見た目の要点。
     *
     * 目標へバネで寄るので、速いほど後ろへ流れ、曲がると遅れて曲がる。
     * 剛体追従にすると隊列がまったく歪まず、「1枚の板が近づいてくる」絵になる。
     */
    void ProcessChargeSoftFollow(TransformComponent &trans, DroneComponent &drone, float dt);

    /**
     * @brief 硬直。吹き飛ばし速度で漂いながら減衰し、制御を失った姿勢を見せる。
     * @note タイマー管理は DroneBrainSystem 側で、ここは動きだけ。
     */
    void ProcessStunned(TransformComponent &trans, DroneComponent &drone, float dt);

    /**
     * @brief 突き抜けた後、上方へ抜けながら減速する離脱カーブ。
     *
     * その場で急旋回させると突撃の勢いが嘘になるので、一度抜けさせてから MoveToTarget に復帰を任せる。
     */
    void ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt);

    // ---- 補助 ----

    /**
     * @brief 予告の震えの位置オフセットを返す。
     *
     * t を取るのは、前フレームぶんを引いて差分だけ動かすため。
     * 足しっぱなしにすると座標へ溜まって群れごと流れていく。
     *
     * @param drone     震わせる機体。位相と震える軸を localIndex から決める。
     * @param t         構え始めてからの経過時間（秒）。
     * @param amplitude 震えの振幅（m）。
     * @return 位置オフセット（m）。
     */
    DirectX::SimpleMath::Vector3 ShiverOffset(const DroneComponent &drone, float t,
                                              float amplitude) const;

    /**
     * @brief slotInSquad から追従の弱さの係数を出す。
     *
     * 後方の機ほど遅れて曲がるので、隊列が先頭から順に曲がっていく。
     * 編隊で飛んでいるように見えるのは主にこの遅れによる。
     *
     * @param drone 係数を出す機体。
     * @return 追従の弱さの係数（0.15〜1）。
     */
    float SlotLag(const DroneComponent &drone) const;

    // ---- Update の先頭で World リソースから写し取る値（並列ループの中で resource を引かないため） ----

    // 陣形攻撃の予告（震え）の調整値。
    //
    // 縦割りのように予備動作でほとんど動かない技は、動きで予告できないので合図の前に震えを出す。
    //
    // 弾幕はこれを使わない。動きで予告する技に足すと、予告が二重になって読めなくなる。
    //
    // 振れ幅の単位は m/s ではなく m。速度で揺らすと、実際の幅が「振幅 ÷ 角速度」になる。
    //
    // 震え方は目で見ないと決まらないので、定数にせず World リソースが持つ。
    DroneShiverTuning m_shiver{};

    // 小隊の追従（SquadTuning から写し取る）。
    float m_followGain     = 10.0f;
    float m_slotLagPerSlot = 0.06f;
};
