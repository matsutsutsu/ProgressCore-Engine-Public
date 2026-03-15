#include "PrimitiveRenderSystem.h"

// 実装に必要なヘッダ
#include "ECS/Core/CCL_World.h"
#include "Engine/Graphics/Renderer/PrimitiveRenderer.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace CCL::ECS;

PrimitiveRenderSystem::PrimitiveRenderSystem() : IfSystem("PrimitiveRenderSystem") {}

void PrimitiveRenderSystem::Update(float dt)
{
    // Worldからレンダラーのポインタを取得
    if (!_world->HasResource<PrimitiveRenderer *>()) return;
    auto *renderer = _world->GetResource<PrimitiveRenderer *>();

    if (!renderer) return;

    ForEach([&](const TransformComponent &trans, const PrimitiveComponent &prim) {
        // 色の決定
        const auto &color = prim.color;
        const auto &world = trans.worldMatrix;

        // タイプに応じた描画コマンドの発行
        switch (prim.type) {
        case PrimitiveType::Box:
            if (prim.isWireframe)
                renderer->DrawWireframeBox(world, prim.size, color);
            else
                renderer->DrawBox(world, prim.size, color);
            break;

        case PrimitiveType::Sphere:
            if (prim.isWireframe)
                renderer->DrawWireframeSphere(world, prim.radius, color);
            else
                renderer->DrawSphere(world, prim.radius, color);
            break;

        case PrimitiveType::Cylinder:
            if (prim.isWireframe)
                renderer->DrawWireframeCylinder(world, prim.radius, prim.height, color);
            else
                renderer->DrawCylinder(world, prim.radius, prim.height, color);
            break;

        case PrimitiveType::Capsule:
            if (prim.isWireframe)
                renderer->DrawWireframeCapsule(world, prim.radius, prim.height, color);
            else
                renderer->DrawCapsule(world, prim.radius, prim.height, color);
            break;

        case PrimitiveType::Cone:
            if (prim.isWireframe)
                renderer->DrawWireframeCone(world, prim.radius, prim.height, color);
            else
                renderer->DrawCone(world, prim.radius, prim.height, color);
            break;
        }
    });
}

// ==========================================
// マクロは必ず .cpp の末尾に1回だけ書く
// ==========================================
REGISTER_RENDER_SYSTEM(PrimitiveRenderSystem, Priority::RenderStage::Main);