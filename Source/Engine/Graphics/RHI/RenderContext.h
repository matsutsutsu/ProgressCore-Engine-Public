#pragma once
// ===========================================================================
// 概要: レンダーパスへ渡す2種類のコンテキスト
//
// 責務:
//   - RenderServices: エンジンの起動から終了まで変わらないサービスへのポインタ
//   - RenderFrameContext: そのフレームだけ有効な出力先・カメラ・サイズ
//
// 備考: 各パスはここから必要なものを受け取り、シングルトンを直接参照しない
// ===========================================================================
#include <d3d11.h>
#include <cstdint>

// TracyD3D11Ctx（TRACY_ENABLE がない構成では void* になる別名）
#include "tracy/TracyD3D11.hpp"

class ResourceManager;
class ShaderRegistry;
class RenderState;
class Camera;
class LightManager;
class ShadowMap;
class ModelConstantBuffers;
class ParticleRenderer;
class TrailRenderer;
class PrimitiveRenderer;
class ShapeRenderer;
class PostProcessManager;
class RenderQueue;

// 各 RenderPass が Initialize のときに受け取る、終了まで変わらないサービス。どのポインタも所有しない
// パスがシングルトンを直接参照しないよう、要るものをここにまとめて渡す
struct RenderServices
{
    // ---- RHI / アセット ----
    ID3D11Device *device              = nullptr;
    ID3D11Buffer *sceneConstantBuffer = nullptr; // CbScene（b8）。GraphicsDevice が持つ

    ResourceManager   *resources   = nullptr;
    ShaderRegistry    *shaders     = nullptr;
    const RenderState *renderState = nullptr;

    // GPU 計測用の Tracy コンテキスト（GraphicsDevice が持つ）
    // ここに載せておけば、RenderPipeline が Graphics シングルトンを参照せずに済み、
    // 下の層が上の層を知らない状態を保てる
    TracyD3D11Ctx tracyContext = nullptr;

    // ---- 描画サブシステム ----
    ModelConstantBuffers *modelConstants    = nullptr;
    ParticleRenderer     *particleRenderer  = nullptr;
    TrailRenderer        *trailRenderer     = nullptr;
    PrimitiveRenderer    *primitiveRenderer = nullptr;
    ShapeRenderer        *shapeRenderer     = nullptr;
    PostProcessManager   *postProcess       = nullptr;

    // ---- シーン情報（オブジェクトは変わらず、中身が毎フレーム変わる） ----
    LightManager *lightManager = nullptr;
    ShadowMap    *shadowMap    = nullptr;
    RenderQueue  *renderQueue  = nullptr;

    UINT screenWidth  = 0; // 画面の幅（px）
    UINT screenHeight = 0; // 画面の高さ（px）
};

// 毎フレーム作り直すコンテキスト。パイプラインの Execute の間だけ有効で、どのポインタも所有しない
struct RenderFrameContext
{
    ID3D11DeviceContext *dc     = nullptr;
    const Camera        *camera = nullptr;

    // ---- 出力先（パイプラインの最終出力。途中のパスで切り替えることがある） ----
    ID3D11RenderTargetView   *targetRTV      = nullptr;
    ID3D11DepthStencilView   *targetDSV      = nullptr;
    ID3D11ShaderResourceView *sceneDepthSRV  = nullptr;
    ID3D11ShaderResourceView *sceneNormalSRV = nullptr;

    // 今のスカイボックス背景（SkyboxComponent のもの、または既定）と、その明るさの倍率
    // EnvironmentSystem が決めて Graphics に渡し、MakeFrameContext がここへ入れる
    // null なら SkyboxPass は空を描かない
    ID3D11ShaderResourceView *skyboxSRV    = nullptr;
    float                     skyIntensity = 1.0f;

    // ---- 描画先のサイズ（targetRTV の実寸, px） ----
    uint32_t viewportW = 0;
    uint32_t viewportH = 0;
};
