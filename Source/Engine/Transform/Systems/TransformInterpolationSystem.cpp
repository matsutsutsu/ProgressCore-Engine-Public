// TransformInterpolationSystem.cpp
#include "TransformInterpolationSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/RenderInterpState.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;

// ---------------------------------------------------------------
// prevPosition/position を alpha 補間して renderWorldMatrix を作る。
// alpha は Framework が毎フレーム RenderInterpStateComponent に書き込む。
// ---------------------------------------------------------------
void TransformInterpolationSystem::Update(float dt)
{
     // 規約準拠：HasResource でガード。alpha は並列ループ前に1回だけ読み、値渡しキャプチャする
    // （ForEachParallel 中に Resource を触らせない＝データレース回避）
    if (!_world->HasResource<RenderInterpState>()) return;
    const float alpha = _world->GetResource<RenderInterpState>().alpha;

    using namespace DirectX;

    ForEachParallel([alpha](TransformComponent &trans) {
        if (trans.isStatic) {
            trans.renderWorldMatrix = trans.worldMatrix;
            return;
        }
        XMVECTOR lerpPos  = XMVectorLerp(XMLoadFloat3(&trans.prevPosition),
                                         XMLoadFloat3(&trans.position), alpha);
        XMVECTOR slerpRot = XMQuaternionSlerp(XMLoadFloat4(&trans.prevRotation),
                                              XMLoadFloat4(&trans.rotation), alpha);
        XMVECTOR scale    = XMLoadFloat3(&trans.scale);
        XMMATRIX m = XMMatrixAffineTransformation(scale, XMVectorZero(), slerpRot, lerpPos);
        XMStoreFloat4x4(&trans.renderWorldMatrix, m);
    });
}

// R00: カメラ(R01)より前。RenderStage の最先頭で補間を確定させる
REGISTER_RENDER_SYSTEM(TransformInterpolationSystem, Priority::RenderStage::R00_Interpolation);