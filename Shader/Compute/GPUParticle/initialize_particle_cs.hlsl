// ===========================================================================
// 概要: GPU パーティクルの初期化。粒子のバッファを作った最初のフレームに、全粒子の最初の状態を作る
//
// 入力: エミッタの設定（cbuffer）
// 出力: 粒子のバッファ（max_count 個）
//
// 備考: 放出のしかたで最初の状態が違う
//         バースト … 全部死んだ状態で待つ。撃たれたら integrate_particle_cs が生む
//         継続（放出中）… 最初から寿命のあちこちまで進めておき、出始めから流れている見た目にする
//         継続（止めてある）… 死んだ状態で待つ。年齢だけ粒子ごとにずらしておく
// ===========================================================================
#include "Shader/Compute/GPUParticle/particle.hlsli"

RWStructuredBuffer<particle> particle_buffer : register(REG_UAV(SLOT_UAV_PARTICLE_BUF));

[numthreads(PARTICLE_CS_THREADS, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint id = dtid.x;
    if (id >= (uint)max_count) return;

    particle p = particle_buffer[id];
    spawn(id, p, 0u); // 初期化なので salt は 0（ここが種の数珠つなぎの始まりになる）

    if (emit_mode == 1)
    {
        // バースト: 死んだ状態で待つ
        p.state   = STATE_DEAD;
        p.color.a = 0.0f;
        p.age     = p.lifespan + 1.0f;
    }
    else if (emit_enabled > 0.5f)
    {
        // 継続（放出中）: 粒子ごとに寿命の違う所まで進めておく
        p.state = STATE_ALIVE;

        float start_ratio = (float)id / (float)max_count;
        p.age = start_ratio * p.lifespan;

        // 進めた年齢の分だけ、等加速度で位置と速度を進める
        p.position += p.velocity * p.age;

        float3 gVec = float3(0.0f, gravity, 0.0f);
        p.position += 0.5f * gVec * p.age * p.age;
        p.velocity += gVec * p.age;
    }
    else
    {
        // 継続（止めてある）: アニメに合わせて後から出すエミッタなど。最初は空にしておく
        // 年齢は粒子ごとにずらす。全部同じ年齢だと、放出を始めた瞬間に一斉に生まれ、
        // 最初の1フレームだけ大量に出てそれ以降出なくなる
        p.state   = STATE_DEAD;
        p.color.a = 0.0f;
        float start_ratio = (float)id / (float)max_count;
        p.age = start_ratio * p.lifespan;
    }

    particle_buffer[id] = p;
}
