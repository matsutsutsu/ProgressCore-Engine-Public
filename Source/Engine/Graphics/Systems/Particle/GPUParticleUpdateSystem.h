#pragma once
// ===========================================================================
// 概要: GPU パーティクルのシミュレーションをコンピュートシェーダーで進める
//       System（R07_PreMain）。
//
// 責務:
//   - エミッタごとに粒子バッファを用意し、初回は InitCS、以後は UpdateCS を Dispatch する
//   - desc から定数バッファを組み立てる（静的な方は desc が変わったときだけ送る）
//   - エミッタごとの乱数の種を配る
//   - コンピュートシェーダーの変更を検知して読み直す（ホットリロード）
//
// 備考: 時間は「描画フレームの dt × Entity の TimeStateComponent.scale」で進めるので、
//       FPS に依らず等速で、ヒットストップやスローにも従う（localDt はロジック1ステップ分の値なので使わない）。
//       描画ステージなのでポーズ中も走るが、ポーズ中は dt を 0 にして粒子を止める。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleBuffer.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"

class ParticleComputeShader;
struct GPUParticleBufferSet;

class GPUParticleUpdateSystem : public CCL::ECS::IfSystem<GPUParticleUpdateSystem,
                                                          CCL::ECS::Write<GPUParticleComponent>,
                                                          CCL::ECS::Read<TransformComponent>,
                                                          CCL::ECS::Read<TimeStateComponent>>
{
public:
    GPUParticleUpdateSystem() : IfSystem("GPUParticleUpdateSystem") {}
    ~GPUParticleUpdateSystem(); // unique_ptr<前方宣言の型> を持つので .cpp で = default にする

    // コンピュートシェーダー・サンプラー・ノイズテクスチャを用意する
    void Initialize() override;

    void Update(float dt) override;

private:
    // そのフレームの、エフェクト全体で共通の値。
    // time や前フレームの姿勢は Entity 単位の値なので、エミッタのループに入る前に
    // 1回だけ決め、各エミッタへ読み取り専用で渡す
    // （time をループ内で進めると、エミッタの数だけ重ねて足されてしまう）。
    struct EffectFrame
    {
        float           time    = 0.0f;                     // 経過時間の合計（今フレームの分を足した後, 秒）
        float           dt      = 0.0f;                     // このフレームの経過時間（秒）。ポーズ中は 0
        GpuData::float3 prevPos = {0.0f, 0.0f, 0.0f};       // 前フレームのワールド座標
        GpuData::float4 prevRot = {0.0f, 0.0f, 0.0f, 1.0f}; // 前フレームのワールド回転
    };

    /**
     * @brief エミッタに重ならない乱数の種を出す。
     *
     * 連番をそのまま返す。シェーダー側が wangHash で散らすので偏らず、実行ごとに同じ順番になるのでデバッグ時に同じ絵を再現できる。
     *
     * @return 乱数の種。0 は「未割り当て」なので返さない。
     */
    GpuData::uint NextInstanceSeed();

    /**
     * @brief 粒子バッファ一式（StructuredBuffer・UAV・SRV・定数バッファ）を作り直す。
     * @param emitter バッファを作り直すエミッタ。
     * @note CreateBuffer を呼ぶので重く、フレームの処理時間が跳ねる原因になる。
     */
    void RebuildParticleBuffer(ParticleEmitter &emitter);

    // ノイズ用の 32^3 の 3D テクスチャを作る
    void CreateNoiseTexture(ID3D11Device *device);

    // 1エミッタ分の InitCS を Dispatch する。終わったら needsGpuInit を false にする
    void DispatchInitCS(ID3D11DeviceContext *dc, ParticleComputeShader *cs,
                        ParticleEmitter &emitter, const TransformComponent &transform,
                        GPUParticleBufferSet *buffers, const EffectFrame &frame);

    // 1エミッタ分の UpdateCS を Dispatch する。定数バッファを更新し、ランプ・ノイズ・サンプラーをバインドする
    void DispatchUpdateCS(ID3D11DeviceContext *dc, ParticleComputeShader *cs,
                          ParticleEmitter &emitter, const TransformComponent &transform,
                          GPUParticleBufferSet *buffers, const EffectFrame &frame);

private:
    ID3D11Device *m_device = nullptr; // Graphics が持つデバイス（所有しない）

    GpuData::uint m_nextInstanceSeed = 1; // 次に出す乱数の種。0 は「未割り当て」なので 1 から始める

    std::unique_ptr<ParticleComputeShader> m_computeShader; // InitCS / UpdateCS（ホットリロード対応）

    Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_sampler;      // ランプ・ノイズ用の線形・ラップのサンプラー
    Microsoft::WRL::ComPtr<ID3D11Texture3D>          m_noiseTexture; // 粒子の動きを乱すノイズ
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_noiseSRV;
};
