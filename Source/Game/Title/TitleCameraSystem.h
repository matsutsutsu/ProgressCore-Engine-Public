#pragma once
// ===========================================================================
// 概要: タイトル画面のカメラ。レベルに置いた TitleCamera の位置を視点として読み、
//       注視点（レベルの TitleLookTarget。無ければ既定値）を見る姿勢を毎フレーム作る。
//
// 責務:
//   - 注視点を軸に視点をゆっくり横と上下に揺らす
//   - 揺らす前の視点・注視点・画角を CompositionBoard に載せる（TitleSwarmSystem が読む）
//   - VirtualCamera の結果に姿勢を書き、Transform には回転だけを書く
//
// 備考: Transform の位置は書き戻さない。揺れた視点を書くと、ギズモで動かしても次のフレームで
//       上書きされて調整できず、File > Save Level で揺れた瞬間の座標が保存されてしまう。
//       そのため Transform の位置と実際の視点は揺れの分（数十cm〜1m）ずれる。構図は揺らす前の
//       位置で決まるので、調整は Transform を動かせばよい。
//       回転だけは書く。向きは注視点から決まるのでレベルに持たせる意味は無いが、書いておくと
//       エディタのギズモとデバッグ表示が実際の向きと合う。
//       止めずに揺らすのは、完全に止まった画面はフリーズしたように見えるため。注視点を軸に
//       少し回すと、手前のプレイヤーと奥の群れに視差が出て奥行きが分かる。
//       TitleScene だけが登録する（REGISTER_* はしない）。カメラは1台の前提で、
//       VirtualCamera を持つエンティティすべてを対象にする。
// ===========================================================================
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <SimpleMath.h>

class TitleCameraSystem
    : public CCL::ECS::IfSystem<TitleCameraSystem, CCL::ECS::Write<VirtualCamera>,
                                CCL::ECS::Write<TransformComponent>>
{
public:
    TitleCameraSystem() : IfSystem("TitleCameraSystem") {}

    void Update(float dt) override;

    void OnGui() override; // 構図の調整用（ENABLE_EDITOR でないビルドでは何もしない）

private:
    // 注視点を返す。目印のエンティティ（kLookTargetName）があればその位置、無ければ既定値
    DirectX::SimpleMath::Vector3 ResolveLookTarget();

    // 揺らす前の視点・注視点・画角を CompositionBoard に載せる
    void PublishBoard(const DirectX::SimpleMath::Vector3 &eye,
                      const DirectX::SimpleMath::Vector3 &lookTarget, const VirtualCamera &vcam);

    // 注視点を軸に横へ回した視点を返す。平行移動にしないのは、
    // 平行移動だと注視点が画面の中で横にずれて構図が崩れるため
    DirectX::SimpleMath::Vector3 DriftedEye(const DirectX::SimpleMath::Vector3 &base,
                                            const DirectX::SimpleMath::Vector3 &pivot) const;

    // VirtualCamera へ姿勢を書き、Transform には回転だけを書く
    void ApplyPose(VirtualCamera &vcam, TransformComponent &trans,
                   const DirectX::SimpleMath::Vector3 &eye,
                   const DirectX::SimpleMath::Vector3 &pivot) const;

    float m_elapsed = 0.0f; // 揺れの経過時間（秒）

    // 注視点の目印。毎フレーム名前で探し直さないよう覚えておく
    CCL::ECS::EntityID m_lookTargetID = CCL::ECS::InvalidEntityID;

    float m_swayDegrees   = 2.2f;  // 横に回す角度の幅（度）。3度を超えるとカメラが動いているように見え始める
    float m_swayPeriod    = 26.0f; // 横の往復の周期（秒）
    float m_riseAmplitude = 0.55f; // 上下の揺れ幅（m）
    float m_risePeriod    = 19.0f; // 上下の周期（秒）。横の周期と割り切れない値にして、繰り返しに見えにくくする

    // 揺れを止める（エディタでの調整用）。揺れている画面で構図を決めるのは難しいので、
    // 止めて決めてから揺れを戻す
    bool m_freezeDrift = false;
};
