#ifndef PARTICLE_HLSLI
#define PARTICLE_HLSLI

// ===========================================================================
// ParticleShared.h が GpuParticle / CbParticleStatic / CbParticleDynamic を定義する。
// このファイルには HLSL 固有の型定義・ヘルパー関数のみを置く。
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/ParticleShared.h"
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
//
// @param salt 生まれ変わりごとに変える追加の種。継続放出で同じ粒子番号を
//             使い回すとき、前回の p.seed を渡して数珠つなぎにする。
//             初期化(InitCS)では 0 を渡す。
//=====================================================================
void spawn(in uint id, inout particle p, in uint salt)
{
    // ---- per-particle 安定シード ----
    //   種は3つの要素から作る。どれが欠けても「同じ絵の繰り返し」が起きる。
    //     id            … 同じエミッタ内で粒子どうしを散らす
    //     instance_seed … 同じプレハブから出した VFX どうしを散らす
    //                     （無いと、並んで出たスパーク2つが寸分違わない）
    //     salt          … 同じ粒子番号の生まれ変わりどうしを散らす
    //                     （無いと、継続放出のエミッタが完全に周期的になる）
    //
    //   足す前に wangHash で散らすのが要点。そのまま足すと
    //   (インスタンス1・粒子5) と (インスタンス2・粒子4) が同じ値になり衝突する。
    p.seed = wangHash(id + wangHash(instance_seed + salt));

    float r_rand     = randUint(p.seed +  1u);
    float a_rand     = randUint(p.seed +  2u);
    float speed_rand = randUint(p.seed +  3u);
    float life_rand  = randUint(p.seed +  4u);
    float size_rand  = randUint(p.seed +  5u);
    float chip_rand  = randUint(p.seed +  6u);
    float ang_rand   = randUint(p.seed +  7u);
    float y_rand     = randUint(p.seed +  8u);

    // ---- サブフレーム補間 ----
    //   前フレーム→現フレームのエミッタ姿勢を s∈[0,1] で補間してスポーンする。
    //   これにより1フレーム内のスポーン群が「その瞬間の線」に固まらず、
    //   剣が動いた軌道上に連続して塗られる（速い振りでも櫛にならない）。
    float  s = randUint(p.seed + 15u);
    float3 emitPos = lerp(prev_world_position, world_position, s);
    float4 q0 = prev_emit_rotation;
    float4 q1 = emit_rotation;
    if (dot(q0, q1) < 0.0f) q1 = -q1;                 // 最短経路（隣接フレームなので nlerp で十分）
    float4 emitRot = normalize(lerp(q0, q1, s) + float4(0, 0, 0, 1e-6f));

    // ---- 発生位置 ----
    float angle = a_rand * 2.0f * PI;

    if (emit_shape > 0.5f)
    {
        // 【線分モード：剣の刃に沿って撒く】
        //   base(emit_local_offset) → tip(emit_local_offset + emit_line_end) を t で均一補間。
        //   emit_radius を刃まわりの筒の太さ(軸に直交する円)として使う。
        //   最後に emitRot(補間したボーンの向き) で回すので、剣を振っても刃に張り付き続ける。
        float  tparam = randUint(p.seed + 14u);
        float3 baseL  = emit_local_offset.xyz;
        float3 tipL   = emit_local_offset.xyz + emit_line_end;
        float3 lineP  = lerp(baseL, tipL, tparam);

        // 刃の軸に直交する2ベクトルを作り、その円周上に太さ分だけ散らす
        float3 axis = normalize(emit_line_end + float3(1e-5f, 1e-5f, 1e-5f));
        float3 ref  = (abs(axis.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 t1   = normalize(cross(axis, ref));
        float3 t2   = cross(axis, t1);
        float  rr   = lerp(emit_radius.x, emit_radius.y, r_rand);
        float3 radial = (t1 * cos(angle) + t2 * sin(angle)) * rr;

        float3 localPos = lineP + radial;
        p.position = emitPos + RotateByQuat(localPos, emitRot);
    }
    else
    {
        // 【ディスクモード：従来の均一楕円（互換）】
        float r = sqrt(lerp(emit_radius.x * emit_radius.x,
                            emit_radius.y * emit_radius.y, r_rand));
        float3 offset;
        offset.x = r * cos(angle) * emit_stretch.x;   // X 軸倍率
        offset.z = r * sin(angle) * emit_stretch.y;   // Z 軸倍率
        // emit_local_offset.w: Y 方向発生揺らぎ幅
        offset.y = emit_local_offset.w * (y_rand * 2.0f - 1.0f);

        p.position = emitPos + emit_local_offset.xyz + offset;
    }

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

    p.velocity     = RotateByQuat(localVel, emitRot);
    p.angular_speed = lerp(emit_angular_speed.x, emit_angular_speed.y, randUint(p.seed + 11u));
    // mass は現在どこからも参照されない予約枠（重力は質量に依らないため）。
    //   将来「効果全体で1本の粒子バッファ」に統合するとき、この4バイトを
    //   「この粒子がどのエミッタのものか」の添字に転用できるよう残してある。
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
    //   size_random はばらつき量。1 なら 0〜指定値の一様ランダム(従来の挙動)、
    //   0 なら指定値ちょうど。衝撃波のように「毎回まったく同じ大きさ」で
    //   出したい効果は 0 にする。誕生と消滅で別の乱数を引くので、
    //   1 のままだと大きく生まれて大きく消えることもあれば逆もある。
    float birth_rand = lerp(1.0f - size_random, 1.0f, size_rand);
    float death_rand = lerp(1.0f - size_random, 1.0f, randUint(p.seed + 13u));
    p.size.x = emit_size.x * birth_rand * particle_scale.x;
    p.size.y = emit_size.y * death_rand * particle_scale.y;

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
