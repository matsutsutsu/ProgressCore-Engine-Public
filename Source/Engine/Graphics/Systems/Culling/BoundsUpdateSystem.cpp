#include "BoundsUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <array>
#include <algorithm>

using namespace CCL::ECS;

// 8頂点完全再計算による正確なAABB生成ヘルパー (Model用)
static DirectX::BoundingBox CalculateAccurateWorldAABB(const DirectX::BoundingBox& localAABB, const DirectX::XMFLOAT4X4& worldMatrix)
{
    std::array<DirectX::XMFLOAT3, 8> corners;
    localAABB.GetCorners(corners.data());

    DirectX::XMMATRIX matWorld = DirectX::XMLoadFloat4x4(&worldMatrix);

    DirectX::XMVECTOR minVec = DirectX::XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX);
    DirectX::XMVECTOR maxVec = DirectX::XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (int i = 0; i < 8; ++i)
    {
        DirectX::XMVECTOR v = DirectX::XMLoadFloat3(&corners[i]);
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
ModelBoundsUpdateSystem::ModelBoundsUpdateSystem() : IfSystem("ModelBoundsUpdateSystem") {}

void ModelBoundsUpdateSystem::Update(float dt)
{
    ForEachParallel([&](const TransformComponent& trans, const ModelComponent& modelComp, BoundingBoxComponent& bounds) {
        Model* model = modelComp.GetModel();
        if (model && model->GetResource()) {
            bounds.worldAABB = CalculateAccurateWorldAABB(model->GetResource()->GetBoundingBox(), trans.worldMatrix);
        }
        });
}

// ====================================================================
// PrimitiveBoundsUpdateSystem の実装
// ====================================================================
PrimitiveBoundsUpdateSystem::PrimitiveBoundsUpdateSystem() : IfSystem("PrimitiveBoundsUpdateSystem") {}

void PrimitiveBoundsUpdateSystem::Update(float dt)
{
    ForEachParallel([&](const TransformComponent& trans, const PrimitiveComponent& prim, BoundingBoxComponent& bounds) {
        // 大まかな最大サイズを計算（Boxならsizeの最大値、他ならradiusかheight）
        float maxLocalScale = (std::max)({ prim.size.x, prim.size.y, prim.size.z, prim.radius, prim.height });

        // ワールド行列からスケール成分を抽出して掛け合わせる（簡易的なAABB半径）
        //   ここで欲しいのは「X軸方向のスケール」と「平行移動」だけ。
        //   XMMatrixDecompose は回転を求めるために反復的な極分解を回すので、
        //   この用途には桁違いに重い。基底ベクトルの長さと平行移動行を直接読む。
        const DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&trans.worldMatrix);
        const DirectX::XMVECTOR pos   = world.r[3];
        const float worldScale   = DirectX::XMVectorGetX(DirectX::XMVector3Length(world.r[0]));
        const float finalExtents = maxLocalScale * worldScale;

        DirectX::XMStoreFloat3(&bounds.worldAABB.Center, pos);
        bounds.worldAABB.Extents = { finalExtents, finalExtents, finalExtents };
        });
}

// ====================================================================
// システムの登録 (Priorityは余計なプレフィックスを含まない元の定義を使用)
// ※CullingSystem等の前（Animationフェーズなど）で実行されるようにします
// ====================================================================
REGISTER_RENDER_SYSTEM(ModelBoundsUpdateSystem, Priority::RenderStage::R04_BoundsUpdate);
REGISTER_RENDER_SYSTEM(PrimitiveBoundsUpdateSystem, Priority::RenderStage::R04_BoundsUpdate);