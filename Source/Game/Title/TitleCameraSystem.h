#pragma once
/// タイトル画面のカメラ。レベルに置かれた TitleCamera の「位置」を構図の入力として読み、
/// 注視点（レベルの TitleLookTarget、無ければ既定値）を見る姿勢を毎フレーム作る。
///
/// なぜ Transform の position を書き戻さないか:
///   揺れを含んだ視点を Transform へ焼くと
///     ・ギズモで動かしても次のフレームで上書きされ、位置を調整できない
///     ・レベル保存（File > Save Level）で「揺れた瞬間の座標」が焼き付く
///   の二つが起きる。揺れは vcam の結果（実際に描画に使われる値）だけに載せ、
///   Transform は「レベルが持つ構図としての視点」のまま触らない。
///   ※その結果、Transform の位置と実写の視点は揺れの分（数十cm〜1m程度）ずれる。
///     構図を決めるのは揺れる前の位置なので、調整はこちらを動かすのが正しい。
///
/// なぜ回転だけは焼くか:
///   向きは常に注視点から決まる従属値で、レベルに持たせても二重管理になるだけ。
///   焼いておくとエディタのギズモとデバッグ表示が実際の向きと一致する。
///
/// なぜ止めずに揺らすか:
///   完全な静止画は「絵」ではなく「フリーズ」に見える。注視点まわりに極小の横流しを足すと、
///   手前のプレイヤーと奥の群れに視差が出て、静止画のままでは出ない奥行きが読める。
///
/// TitleScene だけが登録する（REGISTER_* はしない）。ワールドに1台しかいない前提で、
/// VirtualCamera を持つエンティティすべてを対象にする。
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

    void OnGui() override; // 構図調整用（Shipping では空実装）

private:
    /// 注視点を返す。目印エンティティ(kLookTargetName)が居ればその位置、無ければ既定値。
    DirectX::SimpleMath::Vector3 ResolveLookTarget();

    /// 構図（揺れる前の視点・注視点・画角）を共有ボードへ載せる。
    void PublishBoard(const DirectX::SimpleMath::Vector3 &eye,
                      const DirectX::SimpleMath::Vector3 &lookTarget, const VirtualCamera &vcam);

    /// 注視点を軸に横へ流した視点を返す。平行移動ではなく回転させるのは、
    /// 平行移動だと注視点が画面内で横滑りして構図そのものが崩れるため。
    DirectX::SimpleMath::Vector3 DriftedEye(const DirectX::SimpleMath::Vector3 &base,
                                            const DirectX::SimpleMath::Vector3 &pivot) const;

    /// vcam へ姿勢を書き、Transform には回転だけを焼く。
    void ApplyPose(VirtualCamera &vcam, TransformComponent &trans,
                   const DirectX::SimpleMath::Vector3 &eye,
                   const DirectX::SimpleMath::Vector3 &pivot) const;

    float m_elapsed = 0.0f;

    /// 注視点の目印。毎フレーム名前で探し直さないよう覚えておく。
    CCL::ECS::EntityID m_lookTargetID = CCL::ECS::InvalidEntityID;

    float m_swayDegrees   = 2.2f;  // 横流しの振れ幅[度]。3度を超えると「動くカメラ」に見え始める
    float m_swayPeriod    = 26.0f; // 横流しの往復周期[秒]
    float m_riseAmplitude = 0.55f; // 上下のうねり[m]
    float m_risePeriod    = 19.0f; // 上下の周期[秒]。横と割り切れない値にして繰り返しを感じさせない

    /// 揺れを止める（エディタ専用の調整用）。動いている絵の中で構図を決めるのは難しく、
    /// 止めた状態の1枚で決めてから揺れを戻す方が速い。
    bool m_freezeDrift = false;
};
