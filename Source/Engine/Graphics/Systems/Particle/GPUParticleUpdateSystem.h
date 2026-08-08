#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleBuffer.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"

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
    ~GPUParticleUpdateSystem();   // unique_ptr<前方宣言型> のため .cpp で = default


    void Update(float dt) override;

  private:

    /// @brief 初回のみ呼ばれる。CS管理クラス・サンプラー・ノイズ生成。
    void InitializeParticle(ID3D11Device* device);

    /// @brief 「そのフレームの、効果全体で共通の値」。
    ///        time や前フレーム姿勢はエンティティ単位の値なので、エミッタのループに
    ///        入る前に1回だけ確定させ、各エミッタへ読み取り専用で配る。
    ///        （time をループ内で進めるとエミッタ数ぶん多重に加算されてしまう）
    struct EffectFrame
    {
        float           time    = 0.0f;                  ///< 累積時間（加算済みの値）
        float           dt      = 0.0f;                  ///< このフレームの経過秒
        GpuData::float3 prevPos = { 0.0f, 0.0f, 0.0f };  ///< 前フレームのワールド座標
        GpuData::float4 prevRot = { 0.0f, 0.0f, 0.0f, 1.0f };
    };

    /// @brief エミッタ実体に一意な乱数種を発行する（0 は「未割り当て」なので返さない）。
    /// @note  連番をそのまま返す。シェーダ側が wangHash で散らすので偏りは出ず、
    ///        実行ごとに同じ順序になるためデバッグ時に絵を再現できる。
    GpuData::uint NextInstanceSeed();

    /// @brief StructuredBuffer / UAV / SRV / 定数バッファ一式を作り直す（重い処理）。
    void RebuildParticleBuffer(ParticleEmitter& emitter);

    void CreateNoiseTexture(ID3D11Device* device);

    /// @brief 1エミッタ分の初期化CSをDispatch。終了後 needsGpuInit を false にする。
    void DispatchInitCS(ID3D11DeviceContext* dc, ParticleComputeShader* cs,
        ParticleEmitter& emitter, const TransformComponent& transform,
        GPUParticleBufferSet* buffers, const EffectFrame& frame);

    /// @brief 1エミッタ分の更新CSをDispatch。CB更新・ランプ/ノイズ/サンプラーをバインド。
    void DispatchUpdateCS(ID3D11DeviceContext* dc, ParticleComputeShader* cs,
        ParticleEmitter& emitter, const TransformComponent& transform,
        GPUParticleBufferSet* buffers, const EffectFrame& frame);

private:

    ID3D11Device* m_device = nullptr;
    bool          m_isLoaded = false;

    /// 次に発行する乱数種。0 は「未割り当て」の意味に使うので 1 から始める。
    GpuData::uint m_nextInstanceSeed = 1;

    std::unique_ptr<ParticleComputeShader> m_computeShader; // Init/Update CS（HotReload対応）

    Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_sampler;
    Microsoft::WRL::ComPtr<ID3D11Texture3D>          m_noiseTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_noiseSRV;


};