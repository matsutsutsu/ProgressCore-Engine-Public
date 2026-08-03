#pragma once
#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

/**
 * @brief モデル描画に特化した GPU リソース管理クラス。
 *
 * @details
 *  【設計の責務】
 *   - スケルトン定数バッファ（CbSkeleton）の所有・更新
 *   - CbScene / CbSkeleton のパイプラインへのバインド
 *
 *  【責務外】
 *   - レンダリングパイプラインの管理  → Graphics が担う
 *   - パスの登録・実行               → Graphics が担う
 *
 *  @code
 *   // 呼び出し側（パスから）
 *   renderer->UpdateSkeletonConstants(dc, mesh);
 *
 *   // グローバルリソースのバインド（EnvironmentSystem から）
 *   renderer->BindAllGlobalConstantBuffers(dc);
 *  @endcode
 */
class ModelRenderer
{
public:
    /**
     * @brief スケルトン定数バッファを生成する。
     * @note  パイプラインは別途 Graphics::SetupRenderPipeline() で構築する。
     */
    explicit ModelRenderer(ID3D11Device* device);
    ~ModelRenderer() = default;

    ModelRenderer(const ModelRenderer&) = delete;
    ModelRenderer& operator=(const ModelRenderer&) = delete;

    /**
     * @brief CbScene バッファポインタをキャッシュする（Graphics から 1 回だけ呼ぶ）。
     * @details Graphics が所有する buffer を非所有ポインタとして保持する。
     */
    void SetSceneConstantBuffer(ID3D11Buffer* sceneCB) noexcept { m_sceneCB = sceneCB; }

    /**
     * @brief CbScene・CbSkeleton を VS/PS の全スロットにバインドする。
     * @details EnvironmentSystem::BindGlobalResources() から呼ばれる。
     */
    void BindAllGlobalConstantBuffers(ID3D11DeviceContext* dc);

    /**
     * @brief メッシュのボーン行列を GPU へ転送し、VS スロット SLOT_CB_SKELETON にバインドする。
     * @param mesh スキンドメッシュ（bones が空でも安全）。
     */
    void UpdateSkeletonConstants(ID3D11DeviceContext* dc, const Model::Mesh& mesh);

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbSkeletonBuffer; ///< CbSkeleton (b12)

    /// CbScene (b8) への非所有ポインタ。Graphics が所有し SetSceneConstantBuffer で受け取る。
    ID3D11Buffer* m_sceneCB = nullptr;

    struct CbSkeleton {
        DirectX::XMFLOAT4X4 boneTransforms[MAX_BONES];
    };
    CbSkeleton m_cbSkeletonCache = {};
};