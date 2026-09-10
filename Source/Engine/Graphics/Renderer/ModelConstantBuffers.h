#pragma once
#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

/**
 * @brief モデル描画で使う定数バッファ（CbSkeleton / CbScene）の保持とバインドを担う。
 *
 * @details
 *  【責務】
 *   - スケルトン定数バッファ CbSkeleton (b12) の所有・GPU 転送
 *   - CbScene (b8) / CbSkeleton のパイプラインへのバインド
 *
 *  【責務外】
 *   - 描画そのもの（ドローコール発行）→ 各 IRenderPass が行う
 *   - パスの登録・実行               → Graphics が担う
 *
 *  @note このクラスはメッシュを 1 枚も描かない。定数バッファを用意するだけで、
 *        ドローコールは各 IRenderPass が発行する。
 *
 *  @code
 *   // 描画パスから（スキンドメッシュのボーン行列を送る）
 *   modelConstants->UpdateSkeletonConstants(dc, mesh);
 *
 *   // EnvironmentSystem から（フレーム共通の定数バッファをまとめてバインド）
 *   modelConstants->BindAllGlobalConstantBuffers(dc);
 *  @endcode
 */
class ModelConstantBuffers
{
public:
    /**
     * @brief スケルトン定数バッファを生成する。
     * @note  パイプラインは別途 Graphics::SetupRenderPipeline() で構築する。
     */
    explicit ModelConstantBuffers(ID3D11Device* device);
    ~ModelConstantBuffers() = default;

    ModelConstantBuffers(const ModelConstantBuffers&) = delete;
    ModelConstantBuffers& operator=(const ModelConstantBuffers&) = delete;

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
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbSkeletonBuffer; // CbSkeleton (b12)

    // CbScene (b8) への非所有ポインタ。Graphics が所有し SetSceneConstantBuffer で受け取る。
    ID3D11Buffer* m_sceneCB = nullptr;

    struct CbSkeleton {
        DirectX::XMFLOAT4X4 boneTransforms[MAX_BONES];
    };
    CbSkeleton m_cbSkeletonCache = {};
};