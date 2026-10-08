#pragma once
// ===========================================================================
// 概要: カスケードシャドウに描くメッシュを選び、影描画コマンドを RenderQueue に積む
//       System（R06_Shadow）。
//
// 責務:
//   - ShadowMapConfigComponent の設定をシャドウマップへ反映する
//   - 3つのカスケードのライト行列と、それぞれがワールドで覆う範囲（OBB）を求める
//   - モデル単位・メッシュ単位で、どのカスケードに描くかのビットマスクを作る
//
// 備考: 実際のラスタライズは ShadowPass が行う。
//       NoRenderTag と NoShadowTag の付いた Entity は SetExclude で走査対象から外す。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"

class ShadowMapRenderSystem : public CCL::ECS::IfSystem<ShadowMapRenderSystem,
                                                        CCL::ECS::Read<TransformComponent>,
                                                        CCL::ECS::Read<ModelComponent>,
                                                        CCL::ECS::Read<BoundingBoxComponent>>
{
public:
    ShadowMapRenderSystem();
    virtual ~ShadowMapRenderSystem() = default;

    void Update(float dt) override;

private:
    void RenderShadowMap();
};
