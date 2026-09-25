#include "PrimitiveRenderSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Renderer/PrimitiveRenderer.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

PrimitiveRenderSystem::PrimitiveRenderSystem() : IfSystem("PrimitiveRenderSystem") {}

void PrimitiveRenderSystem::Update(float dt)
{
    if (!m_world->HasResource<PrimitiveRenderer *>()) return;
    auto *renderer = m_world->GetResource<PrimitiveRenderer *>();
    if (!renderer) return;

    ForEachParallel([&](const TransformComponent &trans, const PrimitiveComponent &prim, const VisibilityComponent &vis) {
        if (!vis.isVisible) return;

        DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(&trans.worldMatrix);
        DirectX::XMMATRIX scaleMat = DirectX::XMMatrixIdentity();

        // 寸法 0 のスケールだと法線の計算などで NaN が出るので、最小値で抑える
        float minS = 0.0001f;
        float r    = (std::max)(prim.radius, minS);
        float h    = (std::max)(prim.height, minS);

        // =========================================================
        // 1. 図形ごとのスケール計算
        // =========================================================
        switch (prim.type)
        {
        case PrimitiveType::Box:
            scaleMat = DirectX::XMMatrixScaling(
                (std::max)(prim.size.x, minS),
                (std::max)(prim.size.y, minS),
                (std::max)(prim.size.z, minS));
            break;

        case PrimitiveType::Sphere:
            scaleMat = DirectX::XMMatrixScaling(r, r, r);
            break;

        case PrimitiveType::Cylinder:
        case PrimitiveType::Cone:
            // 円柱・円錐のメッシュは半径 1・高さ 1 なので、そのまま掛ける
            scaleMat = DirectX::XMMatrixScaling(r, h, r);
            break;

        case PrimitiveType::Capsule: {
            // カプセルは拡大すると半球がつぶれるので拡大せず、寸法は下で行列の未使用列に載せて送る
            scaleMat = DirectX::XMMatrixIdentity();
            break;
        }
        }

        // =========================================================
        // 2. 行列を合成し、カプセルの寸法を埋め込む
        // =========================================================
        DirectX::XMMATRIX   finalWorldMat = scaleMat * worldMat;
        DirectX::XMFLOAT4X4 finalWorld;
        DirectX::XMStoreFloat4x4(&finalWorld, finalWorldMat);

        // 定数バッファを増やさずに済ませるため、ワールド行列の未使用列にパラメータを載せて送る
        if (prim.type == PrimitiveType::Capsule)
        {
            float totalHeight    = (std::max)(h, r * 2.0f);
            float cylinderHeight = totalHeight - (r * 2.0f);

            finalWorld._14 = 1.0f;           // [0].w : カプセルであるというフラグ
            finalWorld._24 = r;              // [1].w : カプセルの半径 (Radius)
            finalWorld._34 = cylinderHeight; // [2].w : 円柱部分の長さ
        }
        else
        {
            finalWorld._14 = 0.0f;
        }

        // =========================================================
        // 3. 形と描き方に応じてレンダラーへ積む
        // =========================================================
        if (prim.isWireframe)
        {
            switch (prim.type)
            {
            case PrimitiveType::Box:
                renderer->DrawWireframeBox(finalWorld, prim.color);
                break;
            case PrimitiveType::Sphere:
                renderer->DrawWireframeSphere(finalWorld, prim.color);
                break;
            case PrimitiveType::Cylinder:
                renderer->DrawWireframeCylinder(finalWorld, prim.color);
                break;
            case PrimitiveType::Capsule:
                renderer->DrawWireframeCapsule(finalWorld, prim.color);
                break;
            case PrimitiveType::Cone:
                renderer->DrawWireframeCone(finalWorld, prim.color);
                break;
            }
        }
        else
        {
            switch (prim.type)
            {
            case PrimitiveType::Box:
                renderer->DrawBox(finalWorld, prim.color);
                break;
            case PrimitiveType::Sphere:
                renderer->DrawSphere(finalWorld, prim.color);
                break;
            case PrimitiveType::Cylinder:
                renderer->DrawCylinder(finalWorld, prim.color);
                break;
            case PrimitiveType::Capsule:
                renderer->DrawCapsule(finalWorld, prim.color);
                break;
            case PrimitiveType::Cone:
                renderer->DrawCone(finalWorld, prim.color);
                break;
            }
        }
    });
}

REGISTER_RENDER_SYSTEM(PrimitiveRenderSystem, Priority::RenderStage::R08_Main);