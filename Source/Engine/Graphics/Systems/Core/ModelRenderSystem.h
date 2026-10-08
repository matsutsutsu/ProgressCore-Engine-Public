#pragma once
// ===========================================================================
// 概要: 見えているモデルの描画コマンドを RenderQueue に積む System（R08_Main）。
//
// 責務:
//   - フラスタムカリングで見えているモデルだけを選ぶ
//   - 使うシェーダーを決め、ワールド行列・マテリアルの差し替えと一緒にコマンドへ詰める
//
// 備考: ここでは描画しない。実際のドローコールは OpaquePass などがソートしてからまとめて出す。
//       NoRenderTag の付いた Entity は SetExclude で走査対象から外す。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Graphics/Components/Core/MaterialComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"

class ModelRenderSystem : public CCL::ECS::IfSystem<ModelRenderSystem,
                                                    CCL::ECS::Read<TransformComponent>,
                                                    CCL::ECS::Read<ModelComponent>,
                                                    CCL::ECS::Read<MaterialComponent>,
                                                    CCL::ECS::Read<VisibilityComponent>>
{
public:
    ModelRenderSystem();
    virtual ~ModelRenderSystem() = default;

    void Update(float dt) override;
};
