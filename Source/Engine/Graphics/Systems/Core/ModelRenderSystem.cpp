#include "ModelRenderSystem.h"

#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Components/Core/NoRenderTag.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

#include "Engine/Graphics/Renderer/Core/RenderQueue.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;

ModelRenderSystem::ModelRenderSystem() : IfSystem("ModelRenderSystem")
{
    // 当たり判定だけのジオメトリ（NoRenderTag）を走査対象から外す。描画ループで分岐せずに済む
    SetExclude<NoRenderTag>();
}



void ModelRenderSystem::Update(float dt)
{
    // Instance() の呼び出しをループの外に出す
    auto &resManager  = ResourceManager::Instance();
    auto &renderQueue = RenderQueue::Instance();

    ForEach([&](const TransformComponent  &transform,
                const ModelComponent      &modelComp,
                const MaterialComponent   &mat,
                const VisibilityComponent &vis) {
        // カリングで画面外と判定されたものは積まない
        if (!vis.isVisible) return;

        Model *model = modelComp.GetModel();
        if (!model) return;

        // シェーダーは先頭のマテリアルのものを使う（なければ、モデルを読み込んだときの既定と同じ PBR）
        // スキンメッシュかどうかは、描画側（OpaquePass / ShadowPass）がメッシュごとに mesh.bones で判定する
        uint32_t shaderHash = "PBR"_hash;

        // 差し替えマテリアルがあればそちらを優先する
        if (!mat.overrideMaterials.empty() && mat.overrideMaterials[0].IsValid())
        {
            MaterialData *matData =
                resManager.GetMaterial(mat.overrideMaterials[0]);
            if (matData)
            {
                shaderHash = matData->shaderHash;
            }
        }
        else
        {
            const auto &materials = model->GetMaterials();
            if (!materials.empty() && materials[0].data)
            {
                shaderHash = materials[0].data->shaderHash;
            }
        }

        // =========================================================
        // ここでは描画せず、RenderCommand を RenderQueue に積むだけ
        // 実際のドローコールは OpaquePass 等がソート後にまとめて発行する
        // =========================================================
        RenderCommand cmd;
        cmd.shaderHash = shaderHash;

        cmd.assetHash = modelComp.assetHash;

        cmd.model       = model;
        cmd.worldMatrix = transform.renderWorldMatrix;

        // 差し替えマテリアルは RenderCommand の配列に入る数まで。超えた分は描画に届かないので、1回だけ知らせる
        constexpr size_t kMaxOverrides = RenderCommand::kMaxOverrideMaterials;
        if (mat.overrideMaterials.size() > kMaxOverrides && !m_warnedOverrideLimit)
        {
            CCL_LOG_WARN(LogCategory::Render,
                         "ModelRenderSystem: 上書きのマテリアルが %zu 個あります。先頭の %zu 個までしか使いません。",
                         mat.overrideMaterials.size(), kMaxOverrides);
            m_warnedOverrideLimit = true;
        }
        cmd.overrideCount = static_cast<uint8_t>((std::min)(mat.overrideMaterials.size(), kMaxOverrides));
        for (uint8_t j = 0; j < cmd.overrideCount; ++j)
        {
            cmd.overrideMaterials[j] = mat.overrideMaterials[j];
        }

        cmd.customParams = mat.customParams;

        renderQueue.SubmitOpaque(cmd);
    });
}



REGISTER_RENDER_SYSTEM(ModelRenderSystem, Priority::RenderStage::R08_Main);