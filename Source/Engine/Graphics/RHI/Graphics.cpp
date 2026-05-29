#include "Graphics.h"
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/Camera.h"

#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Resource/ResourceManager.h"  
#include "Engine/Graphics/Renderer/RenderQueue.h"      
#include "Engine/Graphics/Shader/ShaderResources.h"    
#include "Engine/Graphics/Shader/Material/OutlineShader.h"
#include "Engine/Graphics/Shader/Material/ShadowMapShader.h"


// ============================================================
//  シーン定数バッファ構造体 (HLSL: CbScene b8)
//  XMMATRIX メンバを含むため alignas(16) が必須。
//  ない場合コンパイラがパディングを挿入し GPU が壊れた値を読む恐れがある。
// ============================================================
struct alignas(16) CbSceneData
{
	DirectX::XMMATRIX viewProjection;
	DirectX::XMFLOAT4 lightDirection;
	DirectX::XMFLOAT4 lightColor;
	DirectX::XMFLOAT4 cameraPosition;
	DirectX::XMMATRIX view;
	DirectX::XMMATRIX projection;
};

// 初期化
void Graphics::Initialize(HWND hWnd)
{
	this->hWnd = hWnd;
	// 画面のサイズを取得する。
	RECT rc;
	GetClientRect(hWnd, &rc);
	UINT screenWidth = rc.right - rc.left;
	UINT screenHeight = rc.bottom - rc.top;

	this->screenWidth = static_cast<float>(screenWidth);
	this->screenHeight = static_cast<float>(screenHeight);

	HRESULT hr = S_OK;

	// デバイス＆スワップチェーンの生成
	{
		UINT createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
		D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
			D3D_FEATURE_LEVEL_9_3,
			D3D_FEATURE_LEVEL_9_2,
			D3D_FEATURE_LEVEL_9_1,
		};

		// スワップチェーンを作成するための設定オプション
		DXGI_SWAP_CHAIN_DESC swapchainDesc;
		{
			swapchainDesc.BufferDesc.Width = screenWidth;
			swapchainDesc.BufferDesc.Height = screenHeight;
			swapchainDesc.BufferDesc.RefreshRate.Numerator = 60;
			swapchainDesc.BufferDesc.RefreshRate.Denominator = 1;
			swapchainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			swapchainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
			swapchainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
			swapchainDesc.SampleDesc.Count = 1;
			swapchainDesc.SampleDesc.Quality = 0;
			swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapchainDesc.BufferCount = 2;
			swapchainDesc.OutputWindow = hWnd;
			swapchainDesc.Windowed = TRUE;
			swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			swapchainDesc.Flags = 0;
		}

		D3D_FEATURE_LEVEL featureLevel;

		// デバイス＆スワップチェーンの生成
		hr = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			createDeviceFlags,
			featureLevels,
			ARRAYSIZE(featureLevels),
			D3D11_SDK_VERSION,
			&swapchainDesc,
			swapchain.GetAddressOf(),
			device.GetAddressOf(),
			&featureLevel,
			immediateContext.GetAddressOf()
		);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	// レンダーターゲットビューの生成
	{
		// スワップチェーンからバックバッファテクスチャを取得する。
		// ※スワップチェーンに内包されているバックバッファテクスチャは'色'を書き込むテクスチャ。
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d;
		hr = swapchain->GetBuffer(
			0,
			__uuidof(ID3D11Texture2D),
			reinterpret_cast<void**>(texture2d.GetAddressOf()));
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		// バックバッファテクスチャへの書き込みの窓口となるレンダーターゲットビューを生成する。
		hr = device->CreateRenderTargetView(texture2d.Get(), nullptr, renderTargetView.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	// 深度ステンシルビューの生成
	{
		// 深度ステンシル情報を書き込むためのテクスチャを作成する。
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d;
		D3D11_TEXTURE2D_DESC texture2dDesc;
		texture2dDesc.Width = screenWidth;
		texture2dDesc.Height = screenHeight;
		texture2dDesc.MipLevels = 1;
		texture2dDesc.ArraySize = 1;
		texture2dDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		texture2dDesc.SampleDesc.Count = 1;
		texture2dDesc.SampleDesc.Quality = 0;
		texture2dDesc.Usage = D3D11_USAGE_DEFAULT;
		texture2dDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		texture2dDesc.CPUAccessFlags = 0;
		texture2dDesc.MiscFlags = 0;
		hr = device->CreateTexture2D(&texture2dDesc, nullptr, texture2d.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		// 深度ステンシルテクスチャへの書き込みに窓口になる深度ステンシルビューを作成する。
		hr = device->CreateDepthStencilView(texture2d.Get(), nullptr, depthStencilView.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	// ビューポート
	{
		viewport.Width = static_cast<float>(screenWidth);
		viewport.Height = static_cast<float>(screenHeight);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
	}

	// レンダーステート生成
	renderState = std::make_unique<RenderState>(device.Get());

	// レンダラ生成
	primitiveRenderer = std::make_unique<PrimitiveRenderer>(device.Get());
	shapeRenderer = std::make_unique<ShapeRenderer>(device.Get());
	modelRenderer = std::make_unique<ModelRenderer>(device.Get());
	shadowMap = std::make_unique<ShadowMap>(device.Get(), 2048, 2048);
	particleRenderer = std::make_unique<ParticleRenderer>();
    particleRenderer->Initialize(device.Get());
	// (B) LightManager 生成・初期化
	lightManager = std::make_unique<LightManager>();
	lightManager->Initialize(device.Get());

	// (A) PostProcessManager 生成・初期化
	postProcess = std::make_unique<PostProcessManager>();
	postProcess->Initialize(device.Get(), screenWidth, screenHeight);
	// リサイズ時は Graphics::OnResize (未実装) → postProcess->Resize(...) を呼ぶこと


	// ⭕ 変更後: C++クラス不要！ パスを書くだけでどんなシェーダーも追加可能
	auto& registry = ShaderRegistry::Instance();
	
	registry.RegisterMaterialShader("PBR"_hash,
		"Assets/Shader/PBR_VS.cso", L"Shader/Material/PBR/PBR_VS.hlsl",
		"Assets/Shader/PBR_PS.cso", L"Shader/Material/PBR/PBR_PS.hlsl");

	registry.RegisterMaterialShader("Phong"_hash,
		"Assets/Shader/PhongVS.cso", L"Shader/Material/Phong/PhongVS.hlsl",
		"Assets/Shader/PhongPS.cso", L"Shader/Material/Phong/PhongPS.hlsl");

	registry.RegisterMaterialShader("Lambert"_hash,
		"Assets/Shader/LambertVS.cso", L"Shader/Material/Lambert/LambertVS.hlsl",
		"Assets/Shader/LambertPS.cso", L"Shader/Material/Lambert/LambertPS.hlsl");

	registry.RegisterMaterialShader("Basic"_hash,
		"Assets/Shader/BasicVS.cso", L"Shader/Material/Basic/BasicVS.hlsl",
		"Assets/Shader/BasicPS.cso", L"Shader/Material/Basic/BasicPS.hlsl");

	registry.RegisterMaterialShader("Toon"_hash,
		"Assets/Shader/ToonVS.cso", L"Shader/Material/Toon/ToonVS.hlsl",
		"Assets/Shader/ToonPS.cso", L"Shader/Material/Toon/ToonPS.hlsl");

	ShaderRegistry::Instance().RegisterShader<OutlineShader>("Outline"_hash);
	ShaderRegistry::Instance().RegisterShader<ShadowMapShader>("ShadowMap"_hash);

	// ゲームビュー用RTT作成
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = (UINT)screenWidth;
	texDesc.Height = (UINT)screenHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	device->CreateTexture2D(&texDesc, nullptr, gameTex.GetAddressOf());
	device->CreateRenderTargetView(gameTex.Get(), nullptr, gameRTV.GetAddressOf());
	device->CreateShaderResourceView(gameTex.Get(), nullptr, gameSRV.GetAddressOf());

	// シーン定数バッファ作成 (b8用)
    if (!_sceneConstantBuffer) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth         = sizeof(CbSceneData);
        // 16バイトアライメント調整
        if (desc.ByteWidth % 16 != 0) desc.ByteWidth = ((desc.ByteWidth + 15) / 16) * 16;

        desc.Usage          = D3D11_USAGE_DEFAULT;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = 0;

		// device変数はメンバ変数を使用
        HRESULT hr = device->CreateBuffer(&desc, nullptr, _sceneConstantBuffer.GetAddressOf());
        if (FAILED(hr)) {
            // エラーハンドリング (OutputDebugStringなど)
        }
    }

	// ================================================================
	//  RenderPipeline 最終セットアップ
	//  全サブシステム生成後に 1 回だけ実行する。
	//  この時点で全ポインタが有効であること。
	// ================================================================
	{
		RenderServices svc;
		svc.device = device.Get();
		svc.sceneConstantBuffer = _sceneConstantBuffer.Get(); // ← 追加: singleton除去

		svc.renderState = renderState.get();
		svc.resources = &ResourceManager::Instance();
		svc.shaders = &ShaderRegistry::Instance();
		svc.modelRenderer = modelRenderer.get();
		svc.primitiveRenderer = primitiveRenderer.get();
		svc.shapeRenderer = shapeRenderer.get();
		svc.particleRenderer = particleRenderer.get();
		svc.shadowMap = shadowMap.get();
		svc.renderQueue = &RenderQueue::Instance();
		svc.lightManager = lightManager.get();   // (B) 接続
		svc.postProcess = postProcess.get();    // (A) 接続
		modelRenderer->SetupPipeline(svc);
	}
}

// 毎フレーム呼び出す更新処理
void Graphics::Update(Camera* camera)
{
	// 開発中のみ、毎フレームシェーダーのファイル更新を監視する
#if defined(_DEBUG) || defined(ENABLE_EDITOR)
	ShaderRegistry::Instance().UpdateHotReload(GetDevice());
#endif

	// (B) CbLight (b9) を GPU へ転送しパイプラインにバインドする。
	//     シーン側で GetLightManager()->SetDirectionalLight(...) を先に呼ぶこと。
	lightManager->Bind(immediateContext.Get(), SLOT_CB_LIGHT);

	if (!_sceneConstantBuffer || !camera) return;

	CbSceneData data{};

	// --- 行列 ---
	const DirectX::XMFLOAT4X4 vp = camera->GetViewProjectionMatrix();
	data.viewProjection = DirectX::XMLoadFloat4x4(&vp);
	data.view = DirectX::XMLoadFloat4x4(&camera->GetView());
	data.projection = DirectX::XMLoadFloat4x4(&camera->GetProjection());

	// --- カメラ位置 ---
	const auto& pos = camera->GetEye();
	data.cameraPosition = { pos.x, pos.y, pos.z, 1.0f };

	// (B) CbScene のライト情報を LightManager から取得（ハードコード廃止）。
	//     ShadowPass 等が rc.lightManager->GetDirectionalLight() と同じ値を参照する。
	{
		const DirectionalLight& dir = lightManager->GetDirectionalLight();
		data.lightDirection = { dir.direction.x, dir.direction.y, dir.direction.z, 0.0f };
		data.lightColor = { dir.color.x,     dir.color.y,     dir.color.z, dir.intensity };
	}

	immediateContext->UpdateSubresource(
		_sceneConstantBuffer.Get(), 0, nullptr, &data, 0, 0);
}

// ============================================================
//  MakeFrameContext — Render(RenderContext) ブリッジ廃止後の呼び出し側向け
// ============================================================
RenderFrameContext Graphics::MakeFrameContext(const Camera* camera,
	ID3D11RenderTargetView* overrideRTV) noexcept
{
	RenderFrameContext ctx;
	ctx.dc = immediateContext.Get();
	ctx.camera = camera;
	ctx.targetRTV = (overrideRTV != nullptr) ? overrideRTV : gameRTV.Get();
	ctx.targetDSV = depthStencilView.Get();
	ctx.viewportW = static_cast<uint32_t>(screenWidth);
	ctx.viewportH = static_cast<uint32_t>(screenHeight);
	return ctx;
}

// クリア
void Graphics::Clear(float r, float g, float b, float a,bool useGameRTV)
{
	float color[4]{ r, g, b, a };

	if (useGameRTV)
		immediateContext->ClearRenderTargetView(gameRTV.Get(), color);
	else
		immediateContext->ClearRenderTargetView(renderTargetView.Get(), color);

	immediateContext->ClearDepthStencilView(depthStencilView.Get(),
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

// レンダーターゲット設定
void Graphics::SetRenderTargets(bool useGameRTV)
{
	if (useGameRTV)
		immediateContext->OMSetRenderTargets(1, gameRTV.GetAddressOf(), depthStencilView.Get());
	else
		immediateContext->OMSetRenderTargets(1, renderTargetView.GetAddressOf(), depthStencilView.Get());

	immediateContext->RSSetViewports(1, &viewport);
}

// 画面表示
void Graphics::Present(UINT syncInterval)
{
	swapchain->Present(syncInterval, 0);
}



// ============================================================
//  Shutdown
//  生成と逆順に解放する。
//  modelRenderer を先頭にすることで _pipeline.Shutdown() が
//  他サブシステムよりも前に完了することを保証する。
// ============================================================
void Graphics::Shutdown()
{
	if (immediateContext) {
		immediateContext->ClearState();
		immediateContext->Flush();
	}

	// デバッグ用ライブオブジェクトレポート（必要時に有効化）
	if (device) {
		Microsoft::WRL::ComPtr<ID3D11Debug> debug;
		if (SUCCEEDED(device.As(&debug))) {
			// debug->ReportLiveObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
		}
	}

	// ---- GPU リソースの解放（生成と逆順） ----
	_sceneConstantBuffer.Reset();
	gameSRV.Reset();
	gameRTV.Reset();
	gameTex.Reset();

	// パイプラインを最初に解放（各パスの Shutdown が走る）
	modelRenderer.reset();

	// ポストプロセス（ResourceManager 経由で RenderTarget を解放する）
	postProcess.reset();

	// その他サブシステム（生成と逆順）
	lightManager.reset();
	particleRenderer.reset();   // ← 漏れを修正
	shadowMap.reset();
	shapeRenderer.reset();
	primitiveRenderer.reset();

	// レンダーステートは全パス解放後（パスが Shutdown でアクセスする可能性を排除）
	renderState.reset();

	depthStencilView.Reset();
	renderTargetView.Reset();
	swapchain.Reset();
	immediateContext.Reset();
	device.Reset();
}