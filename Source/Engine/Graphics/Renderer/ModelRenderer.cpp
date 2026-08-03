#include "ModelRenderer.h"
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

ModelRenderer::ModelRenderer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(CbSkeleton);
    // 16 バイトアライメント
    if (desc.ByteWidth % 16 != 0) desc.ByteWidth = ((desc.ByteWidth + 15) / 16) * 16;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    HRESULT hr = device->CreateBuffer(&desc, nullptr, m_cbSkeletonBuffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), L"CbSkeleton バッファ生成失敗");
}

void ModelRenderer::BindAllGlobalConstantBuffers(ID3D11DeviceContext* dc)
{
    // CbScene (b8)
    if (m_sceneCB) {
        dc->VSSetConstantBuffers(SLOT_CB_SCENE, 1, &m_sceneCB);
        dc->PSSetConstantBuffers(SLOT_CB_SCENE, 1, &m_sceneCB);
    }
    // CbSkeleton (b12)
    if (m_cbSkeletonBuffer) {
        ID3D11Buffer* skel = m_cbSkeletonBuffer.Get();
        dc->VSSetConstantBuffers(SLOT_CB_SKELETON, 1, &skel);
    }
}

void ModelRenderer::UpdateSkeletonConstants(ID3D11DeviceContext* dc,
    const Model::Mesh& mesh)
{
    if (!m_cbSkeletonBuffer) return;

    if (!mesh.bones.empty()) {
        const size_t count = (std::min)(mesh.bones.size(),
            static_cast<size_t>(MAX_BONES));
        for (size_t i = 0; i < count; ++i) {
            const Model::Bone& bone = mesh.bones[i];
            const DirectX::XMMATRIX mo =
                DirectX::XMLoadFloat4x4(&bone.data->offsetTransform);
            const DirectX::XMMATRIX mw =
                DirectX::XMLoadFloat4x4(&bone.node->worldTransform);
            DirectX::XMStoreFloat4x4(&m_cbSkeletonCache.boneTransforms[i], mo * mw);
        }
    }
    else {
        if (mesh.node)
            m_cbSkeletonCache.boneTransforms[0] = mesh.node->worldTransform;
    }

    dc->UpdateSubresource(m_cbSkeletonBuffer.Get(), 0, nullptr, &m_cbSkeletonCache, 0, 0);

    ID3D11Buffer* buf = m_cbSkeletonBuffer.Get();
    dc->VSSetConstantBuffers(SLOT_CB_SKELETON, 1, &buf);
}