#include "BoundsUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <array>
#include <algorithm>

using namespace CCL::ECS;

// ローカル AABB の8頂点を worldMatrix で変換し、それを包むワールド AABB を作る
static DirectX::BoundingBox CalculateAccurateWorldAABB(const DirectX::BoundingBox &localAABB, const DirectX::XMFLOAT4X4 &worldMatrix)
{
    std::array<DirectX::XMFLOAT3, 8> corners;
    localAABB.GetCorners(corners.data());

    DirectX::XMMATRIX matWorld = DirectX::XMLoadFloat4x4(&worldMatrix);

    DirectX::XMVECTOR minVec = DirectX::XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX);
    DirectX::XMVECTOR maxVec = DirectX::XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (int i = 0; i < 8; ++i)
    {
        DirectX::XMVECTOR v            = DirectX::XMLoadFloat3(&corners[i]);
        DirectX::XMVECTOR transformedV = DirectX::XMVector3TransformCoord(v, matWorld);

        minVec = DirectX::XMVectorMin(minVec, transformedV);
        maxVec = DirectX::XMVectorMax(maxVec, transformedV);
    }

    DirectX::BoundingBox worldAABB;
    DirectX::XMStoreFloat3(&worldAABB.Center, DirectX::XMVectorMultiply(DirectX::XMVectorAdd(minVec, maxVec), DirectX::XMVectorReplicate(0.5f)));
    DirectX::XMStoreFloat3(&worldAABB.Extents, DirectX::XMVectorMultiply(DirectX::XMVectorSubtract(maxVec, minVec), DirectX::XMVectorReplicate(0.5f)));

    return worldAABB;
}



// ====================================================================
// ModelBoundsUpdateSystem の実装
// ====================================================================
void ModelBoundsUpdateSystem::Update(float dt)
{
    // 基準は renderWorldMatrix（補間済み）。ModelRenderSystem が描画に使うのと同じ行列
    //   worldMatrix（固定ステップ）を使うと、カリングが画面に見えている位置ではなく、
    //   最大1固定ステップ分古い位置で判定してしまう
    //   Primitive は描画も worldMatrix なので、そちらは worldMatrix で揃えている
    ForEachParallel([&](const TransformComponent &trans, const ModelComponent &modelComp, BoundingBoxComponent &bounds) {
        Model *model = modelComp.GetModel();
        if (model && model->GetResource())
        {
            bounds.worldAABB = CalculateAccurateWorldAABB(model->GetResource()->GetBoundingBox(), trans.renderWorldMatrix);
        }
    });
}



// ====================================================================
// PrimitiveBoundsUpdateSystem の実装
// ====================================================================
void PrimitiveBoundsUpdateSystem::Update(float dt)
{
    ForEachParallel([&](const TransformComponent &trans, const PrimitiveComponent &prim, BoundingBoxComponent &bounds) {
        // 形によらず、寸法の最大値を半径とする立方体で包む（大きめだが安全側）
        float maxLocalScale = (std::max)({prim.size.x, prim.size.y, prim.size.z, prim.radius, prim.height});

        // ワールド行列から X 軸のスケールと平行移動だけを読む
        //   XMMatrixDecompose は回転を求めるために反復計算の極分解をするので、この用途には重すぎる
        //   基底ベクトルの長さと平行移動の行を直接読む
        const DirectX::XMMATRIX world        = DirectX::XMLoadFloat4x4(&trans.worldMatrix);
        const DirectX::XMVECTOR pos          = world.r[3];
        const float             worldScale   = DirectX::XMVectorGetX(DirectX::XMVector3Length(world.r[0]));
        const float             finalExtents = maxLocalScale * worldScale;

        DirectX::XMStoreFloat3(&bounds.worldAABB.Center, pos);
        bounds.worldAABB.Extents = {finalExtents, finalExtents, finalExtents};
    });
}



// カリング（R05）の前に AABB を更新する
REGISTER_RENDER_SYSTEM(ModelBoundsUpdateSystem, Priority::RenderStage::R04_BoundsUpdate);
REGISTER_RENDER_SYSTEM(PrimitiveBoundsUpdateSystem, Priority::RenderStage::R04_BoundsUpdate);