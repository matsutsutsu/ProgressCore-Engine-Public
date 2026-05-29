#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <memory>
#include "RenderState.h"
#include <string>
#include "Engine/Graphics/Renderer/PrimitiveRenderer.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Renderer/ParticleRenderer.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include "Engine/Graphics/RHI/Light.h"                              // LightManager (B)
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h" // PostProcessManager (A)
#include "Engine/Graphics/RHI/RenderContext.h"                     // RenderFrameContext (C)

class Camera;


// D3Dオブジェクトに名前を付けるヘルパー関数
template <typename T> void SetDebugName(T *child, const std::string &name)
{
    if (child) {
        child->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.c_str());
    }
}

/**
 * @brief グラフィクスシステム全体を管理するシングルトン。
 * @details
 *  デバイス/スワップチェーン/RT/サブシステムの生成・破棄を担う。
 *  【2フェーズ初期化】Initialize → SetupPipeline（ModelRenderer経由）
 *
 *  呼び出し例：
 *  @code
 *    // シーンのレンダリングループ
 *    auto ctx = Graphics::Instance().MakeFrameContext(camera);
 *    Graphics::Instance().GetModelRenderer()->Render(ctx);
 *  @endcode
 */
class Graphics
{
private:
	Graphics() = default;
	~Graphics() = default;

public:
	// インスタンス取得
	static Graphics& Instance()
	{
		static Graphics instance;
		return instance;
	}

	// 初期化
	void Initialize(HWND hWnd);

	/**
	 * @brief カメラ・ライト情報を GPU 定数バッファに転送し、CbLight をパイプラインにバインドする。
	 * @details ライト情報は内部の LightManager から取得する。
	 *          シーン側で GetLightManager()->SetDirectionalLight(...) した後に呼ぶこと。
	 */
    void Update(Camera* camera);

	// クリア
	void Clear(float r, float g, float b, float a,bool useGameRTV);

	// レンダーターゲット設定
	void SetRenderTargets(bool useGameRTV);

	// 画面表示
	void Present(UINT syncInterval);

	// ---- 新 API ヘルパー（ブリッジ廃止後の呼び出し側向け） ----

	/**
	 * @brief 現在の Graphics 状態から RenderFrameContext を構築する。
	 * @param camera     描画に使用するカメラ。
	 * @param overrideRTV 出力 RTV を上書きしたい場合に指定。nullptr なら gameRTV を使用。
	 * @details
	 *  旧 Render(RenderContext) ブリッジを削除した後の呼び出し方：
	 *  @code
	 *    auto ctx = Graphics::Instance().MakeFrameContext(camera);
	 *    modelRenderer->Render(ctx);
	 *    // バックバッファに直接出力したい場合:
	 *    auto ctx = Graphics::Instance().MakeFrameContext(camera, GetBackBufferRTV());
	 *  @endcode
	 */
	RenderFrameContext MakeFrameContext(const Camera* camera,
		ID3D11RenderTargetView* overrideRTV = nullptr) noexcept;

	// ---- アクセサ ----

	HWND GetWindowHandle() { return hWnd; }
	ID3D11Device* GetDevice() { return device.Get(); }
	ID3D11DeviceContext* GetDeviceContext() { return immediateContext.Get(); }

	float GetScreenWidth() const { return screenWidth; }
	float GetScreenHeight() const { return screenHeight; }

	RenderState* GetRenderState() { return renderState.get(); }
	PrimitiveRenderer* GetPrimitiveRenderer() const { return primitiveRenderer.get(); }
	ShapeRenderer* GetShapeRenderer() const { return shapeRenderer.get(); }
	ModelRenderer* GetModelRenderer() const { return modelRenderer.get(); }
	ParticleRenderer *GetParticleRenderer() const { return particleRenderer.get(); }
	ShadowMap* GetShadowMap() const { return shadowMap.get(); }
	/// @brief LightManager へのアクセス。シーン側でライトを設定する際に使用する。
	LightManager* GetLightManager()      const { return lightManager.get(); }
	/// @brief PostProcessManager へのアクセス。ブルーム等のパラメータ設定に使用する。
	PostProcessManager* GetPostProcessManager() const { return postProcess.get(); }

	// 深度ステンシルビューの取得 (これで解決！)
	ID3D11DepthStencilView* GetDepthStencilView() { return depthStencilView.Get(); }

	// バックバッファ(画面)のRTV取得 (ポストプロセスの最終出力先に使う)
	ID3D11RenderTargetView* GetBackBufferRTV() { return renderTargetView.Get(); }

	// シーン定数バッファ (b8: CbScene) を取得する関数
    ID3D11Buffer *GetSceneConstantBuffer() const { return _sceneConstantBuffer.Get(); }

	ID3D11RenderTargetView* GetGameRTV() { return gameRTV.Get(); }
	ID3D11ShaderResourceView* GetGameSRV() { return gameSRV.Get(); }


    void Shutdown(); // これを追加

private:
	HWND											hWnd = nullptr;
	Microsoft::WRL::ComPtr<ID3D11Device>			device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext>		immediateContext;
	Microsoft::WRL::ComPtr<IDXGISwapChain>			swapchain;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView>	renderTargetView;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView>	depthStencilView;
	D3D11_VIEWPORT									viewport = {};

	float	screenWidth = 0;
	float	screenHeight = 0;

	std::unique_ptr<RenderState>					renderState;
	std::unique_ptr<PrimitiveRenderer>				primitiveRenderer;
	std::unique_ptr<ShapeRenderer>					shapeRenderer;
	std::unique_ptr<ModelRenderer>					modelRenderer;
    std::unique_ptr<ParticleRenderer>               particleRenderer;
	std::unique_ptr<ShadowMap> shadowMap;
	std::unique_ptr<LightManager>      lightManager;   ///< (B) ライト管理
	std::unique_ptr<PostProcessManager> postProcess;   ///< (A) ポストプロセス

	// シーン情報用定数バッファ (b8)
	Microsoft::WRL::ComPtr<ID3D11Buffer> _sceneConstantBuffer;

	// --- ゲームビュー用追加 ---
	Microsoft::WRL::ComPtr<ID3D11Texture2D> gameTex;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> gameRTV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> gameSRV;


};
