// ===========================================================================
// 概要: GPU パーティクルの頂点シェーダー。粒子の番号だけを GS へ渡す
//
// 入力: 頂点の番号（頂点バッファなしで、粒子の数だけ点を描く）
// 出力: 粒子の番号
//
// 備考: 粒子のデータは GS がバッファから直接読む
// ===========================================================================
#include "Shader/Compute/GPUParticle/particle.hlsli"

// VS では読まないが、宣言を残している
StructuredBuffer<GpuParticle> particle_buffer : register(REG_SRV(SLOT_SRV_PARTICLE_BUF));

VS_OUT main(uint vertex_id : SV_VERTEXID)
{
    VS_OUT vout;

    vout.vertex_id = vertex_id;

    return vout;
}
