#pragma once
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/RHI/RenderContext.h"
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>
#include "Engine/Graphics/RHI/PipelineState.h"

// 描画に必要な情報の「引換券（コマンド）」
struct ParticleDrawCommand {
    ParticleBufferHandle computeBuffer;
    TextureHandle        texture;
    int                  maxCount;    
    int                  renderMode;  // 0:加算 1:半透明
};


// パーティクルの描画を専任で行うクラス
class ParticleRenderer {
  public:
    ParticleRenderer()  = default;
    ~ParticleRenderer() = default;

    void Initialize(ID3D11Device *device);

    // 毎フレームの頭でバケツを空にする
    void BeginFrame();

    // システムから呼ばれ、バケツにコマンドを積むだけ（ここではDrawしない！）
    void Draw(const ParticleDrawCommand &cmd);

    // =========================================================
    // 最新のコンテキスト分離（Services / FrameCtx）に対応
    // =========================================================
    void Render(const RenderServices& services, const RenderFrameContext& frameCtx);

  private:
    std::vector<ParticleDrawCommand> m_drawCommands;

    // --- 描画用リソース ---
    Microsoft::WRL::ComPtr<ID3D11SamplerState>       sampler;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> defaultTexture;

    bool m_isLoaded = false;

    PipelineState m_psoAdditive;    // 加算パーティクル用
    PipelineState m_psoTransparent; // 半透明パーティクル用
};