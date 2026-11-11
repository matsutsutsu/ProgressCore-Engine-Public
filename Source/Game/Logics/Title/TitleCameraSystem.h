#pragma once
// ===========================================================================
// 概要: タイトル画面のカメラ。レベルの TitleCamera の位置から、注視点を見る姿勢を毎フレーム作る
//
// 責務:
//   - 注視点を軸に、視点をゆっくり横と上下に揺らす
//   - 揺らす前の視点・注視点・画角を CompositionBoard に載せる（TitleSwarmSystem が読む）
//   - VirtualCamera に姿勢を書き、Transform には回転だけを書く
//
// 備考: Transform の位置は書き戻さない。書くとギズモで調整できず、揺れた瞬間の座標が保存される
//       そのため Transform の位置と実際の視点は揺れの分ずれるが、構図は揺らす前の位置で決まる
//       止まった画面はフリーズして見えるので揺らす。注視点を軸に回すと、視差で奥行きが出る
//       TitleScene だけが登録する。編集用のフリーカメラ（CameraBodyFree）は対象から外す
// ===========================================================================
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <SimpleMath.h>

class TitleCameraSystem : public CCL::ECS::IfSystem<
    TitleCameraSystem,
    CCL::ECS::Write<VirtualCamera>,
    CCL::ECS::Write<TransformComponent>>
{
public:
    TitleCameraSystem() : IfSystem("TitleCameraSystem")
    {
        // フリーカメラは CameraFreeControlSystem が動かすので触らない
        SetExclude<CameraBodyFree>();
    }

    void Update(float dt) override;

    void OnGui() override;

private:
    // 注視点を返す。目印のエンティティ（kLookTargetName）があればその位置、なければ既定値
    DirectX::SimpleMath::Vector3 ResolveLookTarget();

    // 揺らす前の視点・注視点・画角を CompositionBoard に載せる
    void PublishBoard(const DirectX::SimpleMath::Vector3 &eye,
                      const DirectX::SimpleMath::Vector3 &lookTarget, const VirtualCamera &vcam);

    // 注視点を軸に、視点を横へ回した位置を返す
    // 平行移動にすると、注視点が画面の中で横にずれて構図が崩れる
    DirectX::SimpleMath::Vector3 DriftedEye(const DirectX::SimpleMath::Vector3 &base,
                                            const DirectX::SimpleMath::Vector3 &pivot) const;

    // VirtualCamera へ姿勢を書き、Transform には回転だけを書く
    void ApplyPose(VirtualCamera &vcam, TransformComponent &trans,
                   const DirectX::SimpleMath::Vector3 &eye,
                   const DirectX::SimpleMath::Vector3 &pivot) const;

    float m_elapsed = 0.0f; // 揺れの経過時間（秒）

    // 注視点の目印。毎フレーム名前で探し直さないよう覚えておく
    CCL::ECS::EntityID m_lookTargetID = CCL::ECS::InvalidEntityID;

    float m_swayDegrees   = 2.2f;  // 横に回す角度の幅（度）。3度を超えると動いて見え始める
    float m_swayPeriod    = 26.0f; // 横の往復の周期（秒）
    float m_riseAmplitude = 0.55f; // 上下の揺れ幅（m）
    float m_risePeriod    = 19.0f; // 上下の周期（秒）。横と割り切れない値にして、繰り返しを目立たせない

    // 揺れを止める。揺れている画面では構図を決めにくいので、エディタで調整するときに使う
    bool m_freezeDrift = false;
};
