#include "AABBDebugDrawSystem.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

AABBDebugDrawSystem::AABBDebugDrawSystem() : IfSystem("AABBDebugDrawSystem") {}

void AABBDebugDrawSystem::Update(float dt)
{
    if (!isDebugVisible) return;

    // ShapeRendererが存在しない場合はスキップ
    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    // ヘッダで指定した Read / Write の型と完全に一致する引数で ForEach を回す
    ForEach([&](const BoundingBoxComponent &bounds, const VisibilityComponent &vis) {
        // カリング判定結果で色を変える
        // 見えている(true)なら緑、カリングで消された(false)なら赤
        DirectX::XMFLOAT4 color = vis.isVisible ? DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f) : // Green
                                      DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);              // Red

        DirectX::XMFLOAT3 zeroEuler = {0.0f, 0.0f, 0.0f};

        // ShapeRendererのDrawBoxで描画 (Center, Rotation, Extents, Color, Wireframe)
        renderer->DrawBox(
            bounds.worldAABB.Center,
            zeroEuler,
            bounds.worldAABB.Extents,
            color);
    });
}

// メイン描画フェーズでギズモ等と一緒に描画する
REGISTER_RENDER_SYSTEM(AABBDebugDrawSystem, Priority::RenderStage::R08_Main);