#pragma once
// ===========================================================================
// 概要: 導入カメラ演出（対象の周りを回り込む）を再生し、終了時にカメラを譲る System。
//
// 責務:
//   - 自動再生・再生要求（CameraIntroPlayRequestEvent）・スキップ入力による再生制御
//   - 進行度に応じたカメラ位置・注視点・画角の計算
//   - 演出中は GameFlowState::cinematic を立て、プレイヤー操作とボスの BT を止めさせる
//   - 終了時（中止・スキップ含む）に CameraIntroFinishedEvent を発行する
//
// 備考: 再生中だけ VirtualCamera.priority を activePriority へ上げ、終わったら
//       idlePriority へ落とす。CameraBrainSystem は最高優先度の vCam を選び、
//       切り替わりを検知したら新しい方の blendTime で補間するので、
//       優先度を下げるだけでプレイヤー追従カメラへ滑らかに戻る。
//       ブレンド処理をこの System に書く必要がない。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Camera/Components/CameraBodyIntroOrbitComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <vector>

class CameraIntroOrbitSystem : public CCL::ECS::IfSystem<
                                   CameraIntroOrbitSystem,
                                   CCL::ECS::Write<VirtualCamera>,
                                   CCL::ECS::Write<CameraBodyIntroOrbit>,
                                   CCL::ECS::Write<TransformComponent>>
{
public:
    CameraIntroOrbitSystem()
        : IfSystem("CameraIntroOrbitSystem")
    {
    }
    virtual ~CameraIntroOrbitSystem() = default;

    void Initialize() override;
    void Update(float dt) override;

private:
    // 0..1 の進行度へカーブを適用する。
    static float ApplyEase(float t, CameraEaseMode mode);

    /**
     * @brief 再生状態を1フレーム進める。
     * @param intro         進める導入演出の状態。
     * @param dt            経過時間（秒）。
     * @param skipRequested このフレームにスキップの入力があったか。
     * @return このフレームに演出が終わったら true。
     * @note 終了時のイベント発行は呼び出し側が行う（ForEach 内から発行しないため）。
     */
    bool AdvancePlayback(CameraBodyIntroOrbit &intro, float dt, bool skipRequested) const;

    /**
     * @brief 進行度に応じたカメラ位置・注視点・画角を vcam と trans へ書き込む。
     * @param vcam  書き込む先のカメラ。
     * @param trans カメラの Transform。
     * @param intro 開始・終了の角度・半径・高さを持つ導入演出の状態。
     * @param pivot 回り込む中心（ワールド）。対象の位置に targetOffset を足したもの。
     * @param eased カーブを当てた後の進行度（0〜1）。開始と終了の値をこれで補間する。
     */
    void ApplyPose(VirtualCamera &vcam, TransformComponent &trans,
                   const CameraBodyIntroOrbit         &intro,
                   const DirectX::SimpleMath::Vector3 &pivot, float eased) const;

    /**
     * @brief 演出を打ち切って待機状態にする（対象消失などの保険）。
     *
     * ここを通らないと、対象がいないのに cinematic が立ったままになり、操作できなくなる。
     * 演出が失敗したときは必ずゲームを進行させる側に倒す。
     *
     * @param vcam  cinematic を下ろすカメラ。
     * @param intro 待機状態へ戻す導入演出の状態。
     */
    void Abort(VirtualCamera &vcam, CameraBodyIntroOrbit &intro) const;
};
