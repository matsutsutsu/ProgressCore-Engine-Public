#pragma once
#include <memory>
#include <string>

#include "Engine/Graphics/RHI/GraphicsDevice.h"
#include "Engine/Graphics/RHI/RenderState.h"
#include "Engine/Graphics/RHI/RenderContext.h"      // RenderServices / RenderFrameContext
#include "Engine/Graphics/Pipeline/Core/RenderPipeline.h" // ← Graphics が直接所有
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Renderer/PrimitiveRenderer.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/Graphics/Renderer/ParticleRenderer.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include "Engine/Graphics/RHI/Light.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"

class Camera;

// D3D デバッグレイヤーでオブジェクト名を設定するヘルパー関数
template <typename T>
void SetDebugName(T* child, const std::string& name)
{
    if (child)
        child->SetPrivateData(WKPDID_D3DDebugObjectName,
            static_cast<UINT>(name.size()), name.c_str());
}

/**
 * @brief 描画サブシステムのコーディネーター（シングルトン）。
 *
 * @details
 *  【分離後の責務】
 *  - GraphicsDevice（_gfxDevice）: D3D11 デバイス/コンテキスト/RT/CbScene を管理
 *  - Graphics 本体             : サブシステム所有 / シェーダー登録 / フレーム更新
 *
 *  公開 API は変更していないため既存の呼び出しコードは無修正でよい。
 */
class Graphics
{
private:
    Graphics() = default;
    ~Graphics() = default;

public:
    static Graphics& Instance()
    {
        static Graphics instance;
        return instance;
    }

    void Initialize(HWND hWnd);
    void Shutdown();

    /**
     * @brief CbScene・CbLight を GPU に転送する（カメラパラメータ不要）。
     * @details _mainCamera を直接参照するため引数なし。
     *          CameraBrainSystem が先に _mainCamera に書き込んでいること。
     */
    void Update();

    /**
    * @brief 登録された全パスを順番に実行する（旧 modelRenderer->Render）。
    * @details パイプラインが Graphics に移ったため、ModelRenderer を介さず直接実行できる。
    */
    void Render(const RenderFrameContext& ctx);

    // ----------------------------------------------------------------
    //  D3D 操作（GraphicsDevice への薄いデリゲート）
    // ----------------------------------------------------------------
    void Clear(float r, float g, float b, float a, bool useGameRTV) { _gfxDevice.Clear(r, g, b, a, useGameRTV); }
    void SetRenderTargets(bool useGameRTV) { _gfxDevice.SetRenderTargets(useGameRTV); }
    void Present(UINT syncInterval) { _gfxDevice.Present(syncInterval); }

    // ----------------------------------------------------------------
    //  アクセサ（GraphicsDevice デリゲート）
    // ----------------------------------------------------------------
    HWND                      GetWindowHandle()       noexcept { return _gfxDevice.GetWindowHandle(); }
    ID3D11Device*             GetDevice()             noexcept { return _gfxDevice.GetDevice(); }
    ID3D11DeviceContext*      GetDeviceContext()       noexcept { return _gfxDevice.GetContext(); }
    float                     GetScreenWidth()   const noexcept { return _gfxDevice.GetWidth(); }
    float                     GetScreenHeight()  const noexcept { return _gfxDevice.GetHeight(); }
    ID3D11DepthStencilView*   GetDepthStencilView()   noexcept { return _gfxDevice.GetDepthStencilView(); }
    ID3D11RenderTargetView*   GetBackBufferRTV()       noexcept { return _gfxDevice.GetBackBufferRTV(); }
    ID3D11Buffer*             GetSceneConstantBuffer() noexcept { return _gfxDevice.GetSceneConstantBuffer(); }
    ID3D11RenderTargetView*   GetGameRTV()             noexcept { return _gfxDevice.GetGameRTV(); }
    ID3D11ShaderResourceView* GetGameSRV()             noexcept { return _gfxDevice.GetGameSRV(); }
 

    // ----------------------------------------------------------------
    //  アクセサ（描画サブシステム）
    // ----------------------------------------------------------------
    RenderState*        GetRenderState()                    { return _renderState.get(); }
    ModelRenderer*      GetModelRenderer()      const       { return _modelRenderer.get(); }
    PrimitiveRenderer*  GetPrimitiveRenderer()  const       { return _primitiveRenderer.get(); }
    ShapeRenderer*      GetShapeRenderer()      const       { return _shapeRenderer.get(); }
    ParticleRenderer*   GetParticleRenderer()   const       { return _particleRenderer.get(); }
    ShadowMap*          GetShadowMap()          const       { return _shadowMap.get(); }
    LightManager*       GetLightManager()       const       { return _lightManager.get(); }
    PostProcessManager* GetPostProcessManager() const       { return _postProcess.get(); }
    Camera*             GetMainCamera()         noexcept    { return &_mainCamera; }

    /**
     * @brief パイプラインに渡された RenderServices への const 参照を返す。
     * @details EnvironmentSystem 等の pipeline 外システム向けアクセサ。
     */
    const RenderServices& GetRenderServices() const noexcept { return _pipeline.GetServices(); }
 

    /**
     * @brief 現在の Graphics 状態から RenderFrameContext を構築するヘルパー。
     * @param overrideRTV nullptr なら gameRTV を使用。
     */
    RenderFrameContext MakeFrameContext(ID3D11RenderTargetView* overrideRTV = nullptr) noexcept;

private:

    // ---- カメラ（Graphics が唯一の所有者） ----
	// ユニークポインタにしない理由は、ECS システムが毎フレームこの Camera の内容を書き換えるため。参照の安定性を保ちたい。
	// ユニークにするとNullの可能性が出てきて面倒
    Camera _mainCamera; ///< Camera() がデフォルト値で初期化。ECS システムが毎フレーム書き込む。

    // ---- D3D11 RHI 層（H で分離） ----
    GraphicsDevice _gfxDevice;

    // ---- レンダリングパイプライン（Graphics が直接所有） ----
    RenderPipeline  _pipeline;
    RenderServices  _renderServices; ///< パイプライン構築後にキャッシュ

    // ---- 描画サブシステム ----
    std::unique_ptr<RenderState>        _renderState;
    std::unique_ptr<PrimitiveRenderer>  _primitiveRenderer;
    std::unique_ptr<ShapeRenderer>      _shapeRenderer;
    std::unique_ptr<ModelRenderer>      _modelRenderer;
    std::unique_ptr<ParticleRenderer>   _particleRenderer;
    std::unique_ptr<ShadowMap>          _shadowMap;
    std::unique_ptr<LightManager>       _lightManager;
    std::unique_ptr<PostProcessManager> _postProcess;

    void SetupRenderPipeline();
    void UnbindAllResources(ID3D11DeviceContext* dc);
};