#include "PrimitivePass.h"
#include "Engine/Graphics/Renderer/PrimitiveRenderer.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/Graphics/SceneView/Camera.h"

void PrimitivePass::Execute(const RenderFrameContext &ctx)
{
    if (!ctx.camera) return;

    // このフレームに積まれた、線で描くデバッグ図形（箱・球・矢印など）を描いて空にする
    if (m_services->shapeRenderer)
    {
        m_services->shapeRenderer->Render(
            ctx.dc, ctx.camera->GetView(), ctx.camera->GetProjection());
    }

    // 行列で渡された、塗り・ワイヤーフレームの図形を描いて空にする
    if (m_services->primitiveRenderer)
    {
        m_services->primitiveRenderer->Render(ctx.dc, *m_services);
    }
}