#pragma once
#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/RHI/RenderContext.h"
#include "Engine/Graphics/Pipeline/Core/RenderPipeline.h"

/**
 * @brief レンダリングパイプラインのオーナー兼スケルトン定数バッファの管理者。
 */
class ModelRenderer
{
public:
    /**
     * @brief Phase 1：スケルトン定数バッファのみ確保する。
     * @param device DirectX デバイス。
     * @note  パイプライン未構築。SetupPipeline を呼ぶまで Render は不可。
     */
    explicit ModelRenderer(ID3D11Device* device);

    /**
     * @brief パイプラインを Shutdown し、全 GPU リソースを解放する。
     */
    ~ModelRenderer();

    ModelRenderer(const ModelRenderer&) = delete;
    ModelRenderer& operator=(const ModelRenderer&) = delete;

    /**
     * @brief Phase 2：パイプラインを組み立てる。全サブシステム生成後に 1 回だけ呼ぶ。
     * @param services 全フィールドが設定済みの永続サービス群。
     *                 エンジン終了まで全ポインタが有効であること。
     * @details
     *  内部で _pipeline.Initialize(services) を先に呼んでから AddPass するため、
     *  services の各ポインタが揃っていない状態で呼ぶと各パスの Initialize が
     *  null 参照で落ちる。呼び出し元（Graphics）で全て埋めてから渡すこと。
     *
     *  @code
     *    // Graphics::Initialize の末尾で：
     *    RenderServices svc;
     *    svc.device        = device.Get();
     *    svc.renderState   = renderState.get();
     *    // ... 全フィールドを設定 ...
     *    modelRenderer->SetupPipeline(svc);
     *  @endcode
     */
    void SetupPipeline(const RenderServices& services);

    /**
     * @brief 毎フレームの描画実行（新 API）。
     * @param ctx フレーム単位の揮発コンテキスト。
     */
    void Render(const RenderFrameContext& ctx);

    /**
    * @brief パイプラインに渡された RenderServices への const 参照を返す。
    * @details EnvironmentSystem 等のパイプライン外システムが
    *          `Graphics::Instance()` を使わずに services に到達するための唯一の入口。
    *  @code
    *    const RenderServices& svc = gfx.GetModelRenderer()->GetServices();
    *    envSystem->BindGlobalResources(dc, svc);
    *  @endcode
    */
    const RenderServices& GetServices() const noexcept;


    // ================================================================
    //  EnvironmentSystem や各 Pass から呼ばれる API
    // ================================================================

    /**
     * @brief CbScene・CbSkeleton 等のグローバル定数バッファを全ステージにバインドする。
     * @details services.sceneConstantBuffer を使用するためシングルトン不要。
     */
    void BindAllGlobalConstantBuffers(ID3D11DeviceContext* dc);

    /**
     * @brief メッシュのボーン行列を GPU へ転送し、VS スロット SLOT_CB_SKELETON にバインドする。
     * @param dc デバイスコンテキスト。
     * @param mesh ボーン情報を含むメッシュ（bones が空でも安全）。
     */
    void UpdateSkeletonConstants(ID3D11DeviceContext* dc, const Model::Mesh& mesh);

private:
    RenderPipeline _pipeline; ///< 全描画パスを管理するパイプライン

    /// スケルトン定数バッファ（ComPtr 所有、手動 Release 不要）
    Microsoft::WRL::ComPtr<ID3D11Buffer> _cbSkeletonBuffer;

    /// CbScene バッファへの非所有ポインタ。Graphics が所有し、SetupPipeline で受け取る。
    /// BindAllGlobalConstantBuffers で使用する（Graphics::Instance() 排除）。
    ID3D11Buffer* _sceneCB = nullptr;

    static constexpr int MAX_BONES = 256;

    struct CbSkeleton {
        DirectX::XMFLOAT4X4 boneTransforms[MAX_BONES];
    };
    CbSkeleton _cbSkeletonCache = {};

    void InitializeGlobalConstantBuffers(ID3D11Device* device);
    void UnbindAllResources(ID3D11DeviceContext* dc);
};