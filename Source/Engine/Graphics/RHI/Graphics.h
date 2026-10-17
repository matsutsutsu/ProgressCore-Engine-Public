#pragma once
// ===========================================================================
// 概要: 描画まわり全体をまとめて持つシングルトン。
//
// 責務:
//   - GraphicsDevice（D3D11 のデバイス・RT・CbScene）と描画サブシステムを作って持つ
//   - シェーダーを登録し、レンダーパイプラインのパス構成を決める
//   - 毎フレーム CbScene と CbLight を GPU へ送り、パイプラインを実行する
//   - デバイスロストを検出し、作り直す
//
// 備考: D3D11 の操作は GraphicsDevice に任せ、ここからは薄く呼ぶだけにしている。
// ===========================================================================
#include <memory>
#include <string>

#include "Engine/Graphics/RHI/GraphicsDevice.h"
#include "Engine/Graphics/RHI/RenderState.h"
#include "Engine/Graphics/RHI/RenderContext.h"
#include "Engine/Graphics/Pipeline/Core/RenderPipeline.h"
#include "Engine/Graphics/Renderer/Core/ModelConstantBuffers.h"
#include "Engine/Graphics/Renderer/PrimitiveRenderer.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/Graphics/Renderer/ParticleRenderer.h"
#include "Engine/Graphics/Renderer/TrailRenderer.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include "Engine/Graphics/SceneView/Light.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"

class Graphics
{
private:
    Graphics()  = default;
    ~Graphics() = default;

public:
    static Graphics &Instance()
    {
        static Graphics instance;
        return instance;
    }

    void Initialize(HWND hWnd);
    void Shutdown();

    /**
     * @brief CbScene と CbLight を GPU へ送る。
     * @note m_mainCamera を読むので、CameraBrainSystem が先にカメラを書いておくこと。
     */
    void Update();

    /**
     * @brief 登録した全パスを順に実行し、最後にリソースのバインドを外す。
     * @param ctx MakeFrameContext() で作った、このフレームだけのコンテキスト
     */
    void Render(const RenderFrameContext &ctx);

    // ============================================================
    // D3D 操作（GraphicsDevice を呼ぶだけ）
    // ============================================================
    void Clear(float r, float g, float b, float a, bool useGameRTV) { m_gfxDevice.Clear(r, g, b, a, useGameRTV); }
    void SetRenderTargets(bool useGameRTV) { m_gfxDevice.SetRenderTargets(useGameRTV); }

    // フレームの先頭で呼ぶ。vsync の待ちをここに集める（Waitable スワップチェーン）
    void WaitForFrameLatency() const noexcept { m_gfxDevice.WaitForFrameLatency(); }

    /**
     * @brief スワップチェーンを提示する。
     * @return デバイスロストを検出したら false。呼び出し側は RecoverFromDeviceLost() を呼ぶ。
     */
    bool Present(UINT syncInterval);

    // ============================================================
    // アクセサ（GraphicsDevice）
    // ============================================================
    HWND                      GetWindowHandle() noexcept { return m_gfxDevice.GetWindowHandle(); }
    ID3D11Device             *GetDevice() noexcept { return m_gfxDevice.GetDevice(); }
    ID3D11DeviceContext      *GetDeviceContext() noexcept { return m_gfxDevice.GetContext(); }
    float                     GetScreenWidth() const noexcept { return m_gfxDevice.GetWidth(); }
    float                     GetScreenHeight() const noexcept { return m_gfxDevice.GetHeight(); }
    ID3D11DepthStencilView   *GetDepthStencilView() noexcept { return m_gfxDevice.GetDepthStencilView(); }
    ID3D11ShaderResourceView *GetDepthSRV() noexcept { return m_gfxDevice.GetDepthSRV(); }
    ID3D11RenderTargetView   *GetBackBufferRTV() noexcept { return m_gfxDevice.GetBackBufferRTV(); }
    ID3D11Buffer             *GetSceneConstantBuffer() noexcept { return m_gfxDevice.GetSceneConstantBuffer(); }
    ID3D11RenderTargetView   *GetGameRTV() noexcept { return m_gfxDevice.GetGameRTV(); }
    ID3D11ShaderResourceView *GetGameSRV() noexcept { return m_gfxDevice.GetGameSRV(); }

    // Tracy の GPU 計測コンテキスト（TracyD3D11Ctx は無効構成では void*）
    TracyD3D11Ctx GetTracyContext() noexcept { return m_gfxDevice.GetTracyContext(); }

    // ============================================================
    // アクセサ（描画サブシステム）
    // ============================================================
    RenderState          *GetRenderState() { return m_renderState.get(); }
    ModelConstantBuffers *GetModelConstantBuffers() const { return m_modelConstants.get(); }
    PrimitiveRenderer    *GetPrimitiveRenderer() const { return m_primitiveRenderer.get(); }
    ShapeRenderer        *GetShapeRenderer() const { return m_shapeRenderer.get(); }
    ParticleRenderer     *GetParticleRenderer() const { return m_particleRenderer.get(); }
    TrailRenderer        *GetTrailRenderer() const { return m_trailRenderer.get(); }
    ShadowMap            *GetShadowMap() const { return m_shadowMap.get(); }
    LightManager         *GetLightManager() const { return m_lightManager.get(); }
    PostProcessManager   *GetPostProcessManager() const { return m_postProcess.get(); }
    Camera               *GetMainCamera() noexcept { return &m_mainCamera; }

    // ============================================================
    // デバイスロスト
    // ============================================================
    /**
     * @brief デバイスロストから復帰する。Shutdown() と Initialize(hWnd) をやり直す。
     *
     * 復帰後は ShaderRegistry と ResourceManager も空になっている。
     * シーンの作り直しは、呼び出し側（Framework）が BaseScene::OnDeviceRecreated() で行う。
     *
     * @return 復帰できたら true
     */
    bool RecoverFromDeviceLost();

    // パイプラインに渡した RenderServices（EnvironmentSystem などパイプラインの外から使う）
    const RenderServices &GetRenderServices() const noexcept { return m_pipeline.GetServices(); }

    /**
     * @brief 今の Graphics の状態から、このフレームの RenderFrameContext を作る。
     * @param overrideRTV 出力先。nullptr ならゲームビューの RTV
     */
    RenderFrameContext MakeFrameContext(ID3D11RenderTargetView *overrideRTV = nullptr) noexcept;

    // NormalPrepass が作った法線バッファを受け取る（所有しない）
    void SetSceneNormalSRV(ID3D11ShaderResourceView *srv) noexcept { m_sceneNormalSRV = srv; }

    // 今フレームのスカイボックス背景を受け取る（EnvironmentSystem が毎フレーム設定する）。
    // MakeFrameContext が RenderFrameContext.skyboxSRV へ渡し、SkyboxPass が使う。
    // 所有しない（実体は ResourceManager か EnvironmentSystem が持つ）。
    void SetSkyboxSRV(ID3D11ShaderResourceView *srv) noexcept { m_pendingSkyboxSRV = srv; }

private:
    // メインカメラ。unique_ptr にしないのは、ECS の System が毎フレーム書き換えるので、
    // null にならず、アドレスが変わらないことを保証したいため。
    Camera m_mainCamera;

    GraphicsDevice m_gfxDevice; // D3D11 のデバイス・RT・CbScene

    RenderPipeline m_pipeline;       // レンダーパスの列と実行
    RenderServices m_renderServices; // パイプラインへ渡す永続サービス（SetupRenderPipeline で埋める）

    // ---- 描画サブシステム ----
    std::unique_ptr<RenderState>          m_renderState;
    std::unique_ptr<PrimitiveRenderer>    m_primitiveRenderer;
    std::unique_ptr<ShapeRenderer>        m_shapeRenderer;
    std::unique_ptr<ModelConstantBuffers> m_modelConstants;
    std::unique_ptr<ParticleRenderer>     m_particleRenderer;
    std::unique_ptr<TrailRenderer>        m_trailRenderer;
    std::unique_ptr<ShadowMap>            m_shadowMap;
    std::unique_ptr<LightManager>         m_lightManager;
    std::unique_ptr<PostProcessManager>   m_postProcess;

    void SetupRenderPipeline();

    /**
     * @brief 既定のレンダーパス構成を m_pipeline へ登録する（登録順 = 実行順）。
     *
     * どのパスをどの順で回すかはこのエンジンの絵作りの仕様なので、汎用の RenderPipeline ではなく描画全体をまとめる Graphics が決める。
     */
    void BuildDefaultRenderPasses();

    // パイプラインの実行後に、共通の定数バッファ・サンプラー・影の SRV を外す
    void UnbindAllResources(ID3D11DeviceContext *dc);

    ID3D11ShaderResourceView *m_sceneNormalSRV   = nullptr; // 法線バッファ（NormalPrepass が持つ。所有しない）
    ID3D11ShaderResourceView *m_pendingSkyboxSRV = nullptr; // 今フレームのスカイボックス（所有しない）
};