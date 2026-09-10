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

/**
 * @brief 描画サブシステムのコーディネーター（シングルトン）。
 *
 * @details
 *  【分離後の責務】
 *  - GraphicsDevice（m_gfxDevice）: D3D11 デバイス/コンテキスト/RT/CbScene を管理
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
     * @details m_mainCamera を直接参照するため引数なし。
     *          CameraBrainSystem が先に m_mainCamera に書き込んでいること。
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
    void Clear(float r, float g, float b, float a, bool useGameRTV) { m_gfxDevice.Clear(r, g, b, a, useGameRTV); }
    void SetRenderTargets(bool useGameRTV) { m_gfxDevice.SetRenderTargets(useGameRTV); }

    // フレーム先頭で呼ぶ vsync待ちの集約点（Waitable スワップチェーン）。
    void WaitForFrameLatency() const noexcept { m_gfxDevice.WaitForFrameLatency(); }

    /**
     * @brief スワップチェーンを提示する。デバイスロスト検出時は false を返す。
     * @return デバイスロストなら false。呼び出し元は IsDeviceLost() を確認すること。
     */
    bool Present(UINT syncInterval);

    // ----------------------------------------------------------------
    //  アクセサ（GraphicsDevice デリゲート）
    // ----------------------------------------------------------------
    HWND                      GetWindowHandle()       noexcept { return m_gfxDevice.GetWindowHandle(); }
    ID3D11Device*             GetDevice()             noexcept { return m_gfxDevice.GetDevice(); }
    ID3D11DeviceContext*      GetDeviceContext()       noexcept { return m_gfxDevice.GetContext(); }
    float                     GetScreenWidth()   const noexcept { return m_gfxDevice.GetWidth(); }
    float                     GetScreenHeight()  const noexcept { return m_gfxDevice.GetHeight(); }
    ID3D11DepthStencilView*   GetDepthStencilView()   noexcept { return m_gfxDevice.GetDepthStencilView(); }
    ID3D11ShaderResourceView* GetDepthSRV()           noexcept { return m_gfxDevice.GetDepthSRV(); }
    ID3D11RenderTargetView*   GetBackBufferRTV()       noexcept { return m_gfxDevice.GetBackBufferRTV(); }
    ID3D11Buffer*             GetSceneConstantBuffer() noexcept { return m_gfxDevice.GetSceneConstantBuffer(); }
    ID3D11RenderTargetView*   GetGameRTV()             noexcept { return m_gfxDevice.GetGameRTV(); }
    ID3D11ShaderResourceView* GetGameSRV()             noexcept { return m_gfxDevice.GetGameSRV(); }

    // Tracyコンテキストへのデリゲート（TracyD3D11Ctx は無効構成で void*）
    TracyD3D11Ctx GetTracyContext()       noexcept { return m_gfxDevice.GetTracyContext(); }

    // ----------------------------------------------------------------
    //  アクセサ（描画サブシステム）
    // ----------------------------------------------------------------
    RenderState*        GetRenderState()                    { return m_renderState.get(); }
    ModelRenderer*      GetModelRenderer()      const       { return m_modelRenderer.get(); }
    PrimitiveRenderer*  GetPrimitiveRenderer()  const       { return m_primitiveRenderer.get(); }
    ShapeRenderer*      GetShapeRenderer()      const       { return m_shapeRenderer.get(); }
    ParticleRenderer*   GetParticleRenderer()   const       { return m_particleRenderer.get(); }
    ShadowMap*          GetShadowMap()          const       { return m_shadowMap.get(); }
    LightManager*       GetLightManager()       const       { return m_lightManager.get(); }
    PostProcessManager* GetPostProcessManager() const       { return m_postProcess.get(); }
    Camera*             GetMainCamera()         noexcept    { return &m_mainCamera; }

    // ----------------------------------------------------------------
    //  デバイスロスト対応
    // ----------------------------------------------------------------
    /**
     * @brief デバイスロスト状態かどうかを返す。
     *        Present() が false を返した後にセットされる。
     */
    bool IsDeviceLost() const noexcept { return m_deviceLost; }
 
    /**
     * @brief デバイスロストからの復帰を試みる。
     *        内部で Shutdown() → Initialize(hWnd) を再実行する。
     *        復帰後は ShaderRegistry と ResourceManager のリセットが完了している。
     * @return 復帰成功なら true。失敗したらプロセスを終了すること。
     */
    bool RecoverFromDeviceLost();

    /**
     * @brief パイプラインに渡された RenderServices への const 参照を返す。
     * @details EnvironmentSystem 等の pipeline 外システム向けアクセサ。
     */
    const RenderServices& GetRenderServices() const noexcept { return m_pipeline.GetServices(); }
 

    /**
     * @brief 現在の Graphics 状態から RenderFrameContext を構築するヘルパー。
     * @param overrideRTV nullptr なら gameRTV を使用。
     */
    RenderFrameContext MakeFrameContext(ID3D11RenderTargetView* overrideRTV = nullptr) noexcept;

    void SetSceneNormalSRV(ID3D11ShaderResourceView* srv) noexcept { m_sceneNormalSRV = srv; }

    // 現在フレームのスカイボックス背景 SRV を預ける（EnvironmentSystem が毎フレーム設定）。
    // MakeFrameContext がこれを RenderFrameContext.skyboxSRV へ渡し、SkyboxPass が使う。
    // 非所有ポインタ（実体は ResourceManager 側が保持）。
    void SetSkyboxSRV(ID3D11ShaderResourceView* srv) noexcept { m_pendingSkyboxSRV = srv; }

private:

    // ---- カメラ（Graphics が唯一の所有者） ----
	// ユニークポインタにしない理由は、ECS システムが毎フレームこの Camera の内容を書き換えるため。参照の安定性を保ちたい。
	// ユニークにするとNullの可能性が出てきて面倒
    Camera m_mainCamera; // Camera() がデフォルト値で初期化。ECS システムが毎フレーム書き込む。

    // ---- D3D11 RHI 層（H で分離） ----
    GraphicsDevice m_gfxDevice;

    // ---- レンダリングパイプライン（Graphics が直接所有） ----
    RenderPipeline  m_pipeline;
    RenderServices  m_renderServices; // パイプライン構築後にキャッシュ

    // ---- 描画サブシステム ----
    std::unique_ptr<RenderState>        m_renderState;
    std::unique_ptr<PrimitiveRenderer>  m_primitiveRenderer;
    std::unique_ptr<ShapeRenderer>      m_shapeRenderer;
    std::unique_ptr<ModelRenderer>      m_modelRenderer;
    std::unique_ptr<ParticleRenderer>   m_particleRenderer;
    std::unique_ptr<ShadowMap>          m_shadowMap;
    std::unique_ptr<LightManager>       m_lightManager;
    std::unique_ptr<PostProcessManager> m_postProcess;

    void SetupRenderPipeline();
    void UnbindAllResources(ID3D11DeviceContext* dc);

    bool m_deviceLost = false; // Present() がデバイスロストを検出するとセットされる

    ID3D11ShaderResourceView* m_sceneNormalSRV = nullptr;

    // 今フレームのスカイボックス SRV（EnvironmentSystem が毎フレーム更新。非所有）。
    ID3D11ShaderResourceView* m_pendingSkyboxSRV = nullptr;
};