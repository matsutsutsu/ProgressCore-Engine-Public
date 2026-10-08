#pragma once
// ===========================================================================
// 概要: GPU パーティクルの描画を受け付け、ParticlePass でまとめて描くクラス。
//
// 責務:
//   - GPUParticleRenderSystem が積んだ描画コマンドを1フレーム分ためる
//   - ブレンドの種類とテクスチャの順に並べ、ステートの切り替えを減らして描く
//
// 備考: 粒子の位置などはコンピュートシェーダーが GPU 上のバッファに書いている。
//       頂点バッファは使わず粒子の数だけ点を描き、ジオメトリシェーダーがそのバッファを読んで
//       各点を四角形に広げる。
// ===========================================================================
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/RHI/RenderContext.h"
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>
#include "Engine/Graphics/RHI/PipelineState.h"

// パーティクル1エミッター分の描画コマンド
struct ParticleDrawCommand
{
    ParticleBufferHandle computeBuffer; // 粒子のバッファと定数バッファ
    TextureHandle        texture;       // 無効なら白
    int                  maxCount;      // 描く点の数（エミッターの最大粒子数）
    int                  renderMode;    // 0 = 加算、1 = 半透明
};

class ParticleRenderer
{
public:
    ParticleRenderer()  = default;
    ~ParticleRenderer() = default;

    // サンプラー・白テクスチャ・PSO を作る
    void Initialize(ID3D11Device *device);

    // ためたコマンドを空にする（BaseScene がフレームの最初に呼ぶ）
    void BeginFrame();

    // コマンドをためる。描くのは Render
    void Draw(const ParticleDrawCommand &cmd);

    // ためたコマンドを描く（ParticlePass から呼ばれる）
    void Render(const RenderServices &services, const RenderFrameContext &frameCtx);

private:
    std::vector<ParticleDrawCommand> m_drawCommands;

    Microsoft::WRL::ComPtr<ID3D11SamplerState>       sampler;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> defaultTexture; // テクスチャが無いときの 1x1 の白

    bool m_isLoaded = false; // Initialize が済んだか

    PipelineState m_psoAdditive;    // 加算
    PipelineState m_psoTransparent; // 半透明
};
