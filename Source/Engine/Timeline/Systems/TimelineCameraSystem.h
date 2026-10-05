#pragma once
// ===========================================================================
// 概要: タイムラインのカメラトラックを評価し、VirtualCamera へ姿勢を書き込む System。
//
// 責務:
//   - 再生位置（TimelineSystem が進める）を描画フレームへ補間し、該当するショットを選ぶ
//   - ショット開始時に基準（役割の位置と向き）を固定する
//   - Truck / Orbit の姿勢を計算し、Blend 指定なら前のショットから補間する
//   - 再生中だけ VirtualCamera の優先度を上げ、終わったら下げてプレイヤーカメラへ譲る
//
// 備考:
//   ショットの切り替えは1台のカメラの姿勢を飛ばすだけで行う。CameraBrainSystem から見ると
//   同じカメラが選ばれ続けるので、切り替えのブレンドが勝手に入らず、カットが確実にカットになる。
//   終了時だけは優先度を下げるので、Brain がプレイヤーカメラ側の blendTime で補間して戻す。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Timeline/Components/TimelinePlayerComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

class TimelineCameraSystem : public CCL::ECS::IfSystem<
                                 TimelineCameraSystem,
                                 CCL::ECS::Write<VirtualCamera>,
                                 CCL::ECS::Write<TimelinePlayerComponent>,
                                 CCL::ECS::Write<TransformComponent>>
{
public:
    TimelineCameraSystem()
        : IfSystem("TimelineCameraSystem")
    {
    }
    virtual ~TimelineCameraSystem() = default;

    void Update(float dt) override;

private:
    /**
     * @brief 再生位置 t のカメラ姿勢を求める。ショットが替わったらその基準を固定し直す。
     * @param player 評価する再生状態。activeShot・shotFrame・lastPose などを書き換える。
     * @param t      評価する再生位置（秒）。
     * @param out    求めた姿勢。
     * @return 役割が解決できず姿勢を出せないときは false。
     */
    bool EvaluateCamera(TimelinePlayerComponent &player, float t, TimelineCameraPose &out) const;

    // ショットに入った瞬間に、基準の位置と向きを固定する。役割が解決できなければ false
    bool EnterShot(TimelinePlayerComponent &player, const CameraShotClip &shot) const;

    /**
     * @brief 1ショットの姿勢を求める（Blend の前）。
     * @param player 固定した基準（shotFrame）を持つ再生状態。
     * @param shot   評価するショット。
     * @param local  ショット先頭からの経過時間（秒）。
     * @param out    求めた姿勢。
     * @return followSubject のショットで役割が見つからなくなったら false。
     */
    bool EvaluateShot(const TimelinePlayerComponent &player, const CameraShotClip &shot, float local,
                      TimelineCameraPose &out) const;

    /**
     * @brief 役割名から、その実体の描画位置と向きを引く。
     * @param player 役割の結び付けを持つ再生状態。
     * @param role   役割名。空なら原点・向き 0 を返す（ワールド座標で書くショット用）。
     * @param outPos 描画補間済みのワールド位置。
     * @param outYaw 水平の向き（ラジアン、atan2(x, z)。0 が +Z）。
     * @return 役割が結び付いていない・実体が消えているときは false。
     */
    bool ResolveRole(const TimelinePlayerComponent &player, const std::string &role,
                     DirectX::SimpleMath::Vector3 &outPos, float &outYaw) const;

    // 姿勢を VirtualCamera と Transform へ書き込む
    void WritePose(VirtualCamera &vcam, TransformComponent &trans, const TimelineCameraPose &pose) const;
};
