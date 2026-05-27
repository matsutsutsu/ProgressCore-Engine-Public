#pragma once
#include <d3d11.h>

class Camera;
class LightManager;
class RenderState;
class ShadowMap;

class ModelRenderer;
class PostProcessManager; 
class ParticleRenderer;   
class PrimitiveRenderer;  
class ShapeRenderer;      

class RenderQueue;
class ResourceManager;
class ShaderRegistry;


// これワールド経由で取らせる？
struct RenderContext
{
	// 全てのポインタを nullptr で初期化
	ID3D11DeviceContext* deviceContext = nullptr;
	const RenderState* renderState = nullptr;
	const Camera* camera = nullptr;
	LightManager* lightManager = nullptr;
	ShadowMap* shadowMap = nullptr;

	// パイプライン化に必要なマネージャー群
	ModelRenderer* modelRenderer = nullptr;
	PostProcessManager* postProcess = nullptr;
	ParticleRenderer* particleRenderer = nullptr;
	PrimitiveRenderer* primitiveRenderer = nullptr;
	ShapeRenderer* shapeRenderer = nullptr;

	// 伝票カウンター
	RenderQueue* renderQueue = nullptr;
	ResourceManager* resourceManager; // リソース管理
	ShaderRegistry* shaderRegistry;  // シェーダー管理

	// =========================================================
	//  パイプラインの最終出力先 (PostProcessがここに絵を返す)
	// =========================================================
	ID3D11RenderTargetView* targetRTV = nullptr;
};
