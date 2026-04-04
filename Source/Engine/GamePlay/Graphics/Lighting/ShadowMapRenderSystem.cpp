#include "ShadowMapRenderSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/GamePlay/Graphics/Lighting/ShadowMapConfigComponent.h"
#include "Engine/Graphics/Core/Camera.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// ShadowMapリソースへのアクセス用
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"

using namespace CCL::ECS;
using namespace DirectX;



ShadowMapRenderSystem::ShadowMapRenderSystem()
    : IfSystem("ShadowMapRenderSystem")
{
}

void ShadowMapRenderSystem::Update(float dt)
{
    // シャドウマップへの描画
    RenderShadowMap();
}

void ShadowMapRenderSystem::RenderShadowMap()
{
    // 1. 必要なリソースの取得
    if (!_world->HasResource<ShadowMap*>()) return;
    ShadowMap* shadowMap = _world->GetResource<ShadowMap*>();

    // 2. View を使ったデータの同期 (Single Source of Truth)
    // テンプレートシグネチャに縛られず、設定コンポーネントだけを検索してパラメータを上書きする
    auto view = _world->View<ShadowMapConfigComponent>();
    for (auto entity : view) {
        auto* config = _world->GetComponent<ShadowMapConfigComponent>(entity);
        if (config) {
            shadowMap->params.shadowBias = config->shadowBias;
            shadowMap->params.shadowColor = config->shadowColor;
            shadowMap->params.cascadeSplits[0] = config->cascadeSplits.x;
            shadowMap->params.cascadeSplits[1] = config->cascadeSplits.y;
            shadowMap->params.cascadeSplits[2] = config->cascadeSplits.z;
            break; // 設定を持つエンティティは1つだけのはずなので、見つけたら即終了（高速化）
        }
    }

  
    auto& renderQueue = RenderQueue::Instance();

    ForEach([&](const TransformComponent &trans, const ModelComponent &model) {
        // モデルまたはメッシュがない場合はスキップ
        if (!model.modelPtr) return;

        // 全てのメッシュを影描画キューに登録
        for (const auto& mesh : model.modelPtr->GetMeshes()) {

            // 影用の伝票を作成
            ShadowCommand cmd;
            cmd.model = model.modelPtr.get();
            cmd.mesh = &mesh;
            cmd.worldMatrix = trans.worldMatrix;

            // カウンターに提出
            renderQueue.SubmitShadow(cmd);
        }
        
    });
}


REGISTER_RENDER_SYSTEM(ShadowMapRenderSystem, Priority::RenderStage::R06_Shadow);