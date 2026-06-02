#ifndef PARTICLE_HLSLI
#define PARTICLE_HLSLI

// ===========================================================================
// ParticleShared.h が GpuParticle / CbParticleStatic / CbParticleDynamic を定義する。
// このファイルには HLSL 固有の型定義・ヘルパー関数のみを置く。
// ===========================================================================
#include "Engine/Graphics/Shader/ParticleShared.h"
#include "Shader/Common/Scene.hlsli"   // CbScene (viewProjection, gProjection 等)

// C++ 側は GpuData::GpuParticle、HLSL 内コードは "particle" で統一
typedef GpuParticle particle;

//--------------------------------------------
// VS → GS 受け渡し
//--------------------------------------------
struct VS_OUT { uint vertex_id : VERTEXID; };

//--------------------------------------------
// GS → PS 受け渡し
//--------------------------------------------
struct GS_OUT
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD;
    uint   type     : TEXCOORD1;  // 0:Point 1:Quad
};

//--------------------------------------------
// 定数
//--------------------------------------------
#define PI           3.14159265358979f
#define NUMTHREADS_X 16
#define STATE_DEAD   0
#define STATE_ALIVE  1

//=====================================================================
// 疑似乱数 (Wang Hash ベース、高品質・独立ストリーム)
//=====================================================================
uint wangHash(uint s)
{
    s = (s ^ 61u) ^ (s >> 16u);
    s *= 9u;
    s = s ^ (s >> 4u);
    s *= 0x27d4eb2du;
    s = s ^ (s >> 15u);
    return s;
}

// [0, 1) の float に変換
float randUint(uint s)
{
    return (wangHash(s) & 0x00FFFFFFu) / 16777216.0f;
}

// クォータニオン回転
float3 RotateByQuat(float3 v, float4 q)
{
    return v + 2.0f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

//=====================================================================
// spawn() — 粒子1個を初期化する
//
// particle_scale は誕生/消滅サイズにベイクして p.scale フィールドを廃止。
// p.seed を per-particle 安定シードとして設定。
//=====================================================================
void spawn(in uint id, inout particle p)
{
    // per-particle 安定シード (id を基底とした独立ストリーム)
    p.seed = wangHash(id);

    float r_rand     = randUint(p.seed +  1u);
    float a_rand     = randUint(p.seed +  2u);
    float speed_rand = randUint(p.seed +  3u);
    float life_rand  = randUint(p.seed +  4u);
    float size_rand  = randUint(p.seed +  5u);
    float chip_rand  = randUint(p.seed +  6u);
    float ang_rand   = randUint(p.seed +  7u);
    float y_rand     = randUint(p.seed +  8u);

    // ---- 発生位置（均一楕円） ----
    float angle = a_rand * 2.0f * PI;
    float r     = sqrt(lerp(emit_radius.x * emit_radius.x,
                            emit_radius.y * emit_radius.y, r_rand));

    float3 offset;
    offset.x = r * cos(angle) * emit_stretch.x;   // X 軸倍率
    offset.z = r * sin(angle) * emit_stretch.y;   // Z 軸倍率
    // emit_local_offset.w: Y 方向発生揺らぎ幅
    offset.y = emit_local_offset.w * (y_rand * 2.0f - 1.0f);

    // emit_rotation / world_position は CbParticleDynamic から
    p.position = world_position + emit_local_offset.xyz + offset;

    // ---- 速度（円錐放出） ----
    float phi       = 2.0f * PI * randUint(p.seed + 9u);
    float theta     = lerp(cone_angle.x, cone_angle.y, randUint(p.seed + 10u));
    float sin_theta = sin(theta);
    float cos_theta = cos(theta);

    float3 localVel;
    localVel.x = sin_theta * cos(phi);
    localVel.y = cos_theta;
    localVel.z = sin_theta * sin(phi);
    localVel  *= lerp(emit_speed.x, emit_speed.y, speed_rand);

    p.velocity     = RotateByQuat(localVel, emit_rotation);
    p.angular_speed = lerp(emit_angular_speed.x, emit_angular_speed.y, randUint(p.seed + 11u));
    p.mass          = 1.0f;
    p.angle         = PI * ang_rand;
    p.color         = float4(1, 1, 1, 1);

    // ---- 寿命 / 年齢 ----
    p.lifespan = lerp(lifespan.x, lifespan.y, life_rand);
    p.age      = (emit_mode == 1)
                 ? 0.0f
                 : -lerp(spawn_delay.x, spawn_delay.y, randUint(p.seed + 12u));
    p.state    = STATE_ALIVE;

    // ---- サイズ (particle_scale をベイク) ----
    // size.x: 誕生時サイズ, size.y: 消滅時サイズ
    p.size.x = emit_size.x * size_rand               * particle_scale.x;
    p.size.y = emit_size.y * randUint(p.seed + 13u)  * particle_scale.y;

    // ---- スプライトシート初期コマ ----
    int total = (int)(sprite_sheet_grid.x * sprite_sheet_grid.y);
    if (sprite_anim_mode == 1)
        p.chip = 0;                              // Life モード: 最初のコマから
    else
        p.chip = (uint)(chip_rand * (float)total); // Random モード
}

//=====================================================================
// フェード補間
//=====================================================================
float fade_in(float duration, float age, float exponent)
{
    return pow(smoothstep(0.0f, 1.0f, age / duration), exponent);
}
float fade_out(float duration, float age, float ls, float exponent)
{
    return pow(smoothstep(0.0f, 1.0f, (ls - age) / duration), exponent);
}

#endif // PARTICLE_HLSLI
