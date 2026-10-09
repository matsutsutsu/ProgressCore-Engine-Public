#pragma once
// ===========================================================================
// 概要: ドローンの動きの共通部品。DroneMovementSystem と、突撃中の動き方クラスの両方が使う。
//
// 責務:
//   - 進行方向から姿勢を作り、機首を少しずつ向け直す
//   - 持ち場へ戻る動き（MoveToTarget）と、持ち場を保つ動き（Idle）
//
// 備考:
//   Idle と MoveToTarget の動きをここに置くのは、突撃中の動き方クラスも使うため。
//   直進は発射待ちの間に持ち場を追い（MoveToSlot）、剛体追従は動き切った後に Idle のバネへ渡す（HoldSlot）。
//
//   各関数は drone.currentVelocity と trans.rotation だけを決める（予告の震えだけは位置へ差分を足す）。
//   位置への積分は DroneMovementSystem が後で行う。
//
//   補間係数は必ず (std::min)(k * dt, 1.0f) で止める。1 を超えると外挿になって
//   目標を通り越す（globalScale を 1 より大きくできるので dt 側では保証されない）。
// ===========================================================================
#include "SimpleMath.h"

#include "Game/Logics/AI/Drone/Motion/IChargeMotion.h"

namespace CCL::AI::Motion
{
    /**
     * @brief 進行方向から姿勢を作る。yaw の式をここ1箇所に閉じ込める。
     *
     * ドローンのモデルは前方が -Z。CreateFromYawPitchRoll は行ベクトルで v·Rx(pitch)·Ry(yaw) の順に掛かるので、
     *   (0,0,-1)·Rx(p) = (0, +sin p, -cos p)        ← pitch = asin(look.y) と一致する
     *   ここへ Ry(θ) = (-cos p·sin θ, sin p, -cos p·cos θ)
     * となり、look と突き合わせると θ = atan2(-x, -z)。
     *
     * +Z 前提の atan2(x, z) や atan2(x, -z) と混ぜると Z軸に対する鏡像になり、状態が切り替わるたびに機首が最大180°反転する。
     * ±Z を向いているときだけ一致するので気づきにくい。
     *
     * @param look         向かせたい方向。includePitch=true のときだけ正規化が要る（asin に渡すため）。
     * @param includePitch 上下の傾きも付けるか。false は Idle のように「水平だけ向かせたい」場面用。
     * @return 機体の姿勢。
     */
    DirectX::SimpleMath::Quaternion LookToRotation(const DirectX::SimpleMath::Vector3 &look, bool includePitch);

    /**
     * @brief 機首を look の向きへ、turnRate の速さで少しずつ向け直す。
     * @param trans        向け直す機体の Transform。rotation を書く。
     * @param look         向かせたい方向。
     * @param includePitch 上下の傾きも付けるか（LookToRotation へそのまま渡す）。
     * @param turnRate     向け直す速さ [1/s]。
     * @param dt           この機体の経過時間 [秒]。
     */
    void TurnToward(TransformComponent &trans, const DirectX::SimpleMath::Vector3 &look, bool includePitch,
                    float turnRate, float dt);

    // 陣形へ戻る（MoveToTarget）。隊列を「直す」局面なので、Idle より強いバネで寄せる。
    void MoveToSlot(TransformComponent &trans, DroneComponent &drone, float dt, const MotionContext &ctx);

    // 陣形を保つ（Idle）。弱いバネで寄せ、ホバーの揺れと予告の震えを足す。
    void HoldSlot(TransformComponent &trans, DroneComponent &drone, float dt, const MotionContext &ctx);

} // namespace CCL::AI::Motion
