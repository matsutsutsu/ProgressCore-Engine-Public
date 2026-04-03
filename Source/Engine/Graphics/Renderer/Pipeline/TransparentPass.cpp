#include "TransparentPass.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include <algorithm>

void TransparentPass::Initialize(ID3D11Device* device) {
    _psoTransparent = PipelineState(PipelineStateDesc::DefaultTransparent());
    _instanceBuffer.Create(device, 8192);
}

void TransparentPass::Execute(const RenderContext& rc){
#ifdef TRACY_ENABLE
    ZoneScopedN("Pass: Transparent");
#endif

    ID3D11DeviceContext* dc = rc.deviceContext;

    // ★修正：キューとレンダラーは rc から取り出す
    RenderQueue* queue = rc.renderQueue;
    ModelRenderer* renderer = rc.modelRenderer;

    if (!queue || !renderer) return; // 安全対策

    // =========================================================
    // 半透明描画でも影のデータを読み込めるようにセットする
    // =========================================================
    if (rc.shadowMap) {
        rc.shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }

    _psoTransparent.Apply(dc, rc.renderState);

    // ECSからの直通分と、OpaquePassからの回し分を合体
    auto allTrans = queue->GetDeferredTransparentCommands();
    for (const auto& t : queue->GetTransparentCommands()) {
        allTrans.push_back(t);
    }

    std::sort(allTrans.begin(), allTrans.end(),
        [](const TransparencyCommand& lhs, const TransparencyCommand& rhs) {
            return lhs.distance > rhs.distance;
        });

    for (const TransparencyCommand& info : allTrans) {
        Shader* shader = ShaderRegistry::Instance().GetShader(info.shaderHash, Graphics::Instance().GetDevice());
        if (!shader) continue;

        shader->Begin(rc);

        const Model::Mesh& mesh = *info.mesh;
        Model::Mesh& mutableMesh = const_cast<Model::Mesh&>(mesh);
        Model::Material* originalMatPtr = mutableMesh.material;
        Model::Material tempMatWrapper;

        bool applyOverride = info.overrideMaterial.IsValid();
        if (applyOverride) {
            MaterialData* ptr = ResourceManager::Instance().GetMaterial(info.overrideMaterial);
            if (ptr) {
                tempMatWrapper.data = std::shared_ptr<MaterialData>(ptr, [](MaterialData*) {});
                tempMatWrapper.name = "TranspOverride";
                mutableMesh.material = &tempMatWrapper;
            }
            else {
                applyOverride = false;
            }
        }

        InstanceData singleData;
        singleData.world = info.worldMatrix;
        singleData.customParams = info.customParams;

        std::vector<InstanceData> temp = { singleData };
        _instanceBuffer.Write(dc, temp);
        _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        renderer->UpdateSkeletonConstants(dc, mesh);
        shader->Update(rc, mesh);

        UINT stride = sizeof(ModelResource::Vertex);
        UINT offset = 0;
        dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);

        if (applyOverride) mutableMesh.material = originalMatPtr;

        _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        shader->End(rc);
    }
}