#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleBuffer.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"

class ParticleComputeShader; // 前方宣言
struct GPUParticleBufferSet;

// パーティクルのシミュレーションを行うシステム
// SystemPriority::Render グループで実行推奨 (描画前)
class GPUParticleUpdateSystem : public CCL::ECS::IfSystem<GPUParticleUpdateSystem,
                                    CCL::ECS::Write<GPUParticleComponent>,
                                    CCL::ECS::Read<TransformComponent>,
                                    CCL::ECS::Read<TimeStateComponent>> {
  public:
    GPUParticleUpdateSystem() : IfSystem("GPUParticleUpdateSystem") {}
    ~GPUParticleUpdateSystem();   // ★unique_ptr<前方宣言型> のため .cpp で = default


    void Update(float dt) override;

  private:

    /// @brief 初回のみ呼ばれる。CS管理クラス・サンプラー・ノイズ生成。
    void InitializeParticle(ID3D11Device* device);

    /// @brief StructuredBuffer / UAV / SRV / 定数バッファ一式を作り直す（重い処理）。
    void RebuildParticleBuffer(GPUParticleComponent& particle);

    void CreateNoiseTexture(ID3D11Device* device);

    /// @brief 1エミッタ分の初期化CSをDispatch。終了後 needsGpuInit を false にする。
    void DispatchInitCS(ID3D11DeviceContext* dc, ParticleComputeShader* cs,
        GPUParticleComponent& particle, const TransformComponent& transform,
        GPUParticleBufferSet* buffers);

    /// @brief 1エミッタ分の更新CSをDispatch。CB更新・ランプ/ノイズ/サンプラーをバインド。
    void DispatchUpdateCS(ID3D11DeviceContext* dc, ParticleComputeShader* cs,
        GPUParticleComponent& particle, const TransformComponent& transform,
        GPUParticleBufferSet* buffers, float dt);

private:

    ID3D11Device* _device = nullptr;
    bool          _isLoaded = false;

    std::unique_ptr<ParticleComputeShader> _computeShader; // Init/Update CS（HotReload対応）

    Microsoft::WRL::ComPtr<ID3D11SamplerState>       _sampler;
    Microsoft::WRL::ComPtr<ID3D11Texture3D>          _noiseTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _noiseSRV;


};