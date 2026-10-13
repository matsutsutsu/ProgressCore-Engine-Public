#pragma once
// ===========================================================================
// 概要: 固定ステップの物理と可変フレームの描画の間を補間し、描画用ワールド行列を確定する System。
//
// 責務:
//   - prevPosition/position（回転は prevRotation/rotation）を alpha で補間し、
//     renderWorldMatrix を作る
//   - 親子関係も TransformUpdateSystem と同じ順序で解決する
//
// 備考: Fix Your Timestep パターンの描画側。alpha は World リソースの RenderInterpState から読む。
//       ロジック用の worldMatrix には一切触れないので、物理・当たり判定への副作用は無い。
//       カメラが補間後の位置を追従できるよう、R01_Camera より前（R00）で実行する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"

class TransformInterpolationSystem : public CCL::ECS::IfSystem<TransformInterpolationSystem,
                                                               CCL::ECS::Write<TransformComponent>>
{
public:
    TransformInterpolationSystem() : IfSystem("TransformInterpolationSystem") {}
    virtual ~TransformInterpolationSystem() = default;

    void Update(float dt) override;

private:
    // 非ルートの解決で使う、描画用ワールド行列の作業領域（並びは TransformHierarchyCache と同じ）。
    //   キャッシュ側の作業領域（WorldAt/StoreWorld）はロジック用の値が入っているので、
    //   汚さないよう描画用は別に持つ。
    std::vector<DirectX::XMFLOAT4X4> m_renderWorldDense;
};