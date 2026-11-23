#ifndef PARTICLE_HLSLI
#define PARTICLE_HLSLI

// ===========================================================================
// 概要: GPU パーティクルの5つのシェーダー（初期化・更新・VS・GS・PS）が共有する型と関数
//
// 備考: 粒子の構造体と2つの cbuffer は ParticleShared.h で C++ と共有している
//       ここには HLSL だけの型と、粒子を生む spawn() などの関数を置く
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/ParticleShared.h"
#include "Shader/Common/Scene.hlsli"

// C++ では GpuData::GpuParticle、HLSL では particle と呼ぶ
typedef GpuParticle particle;

// VS → GS。粒子の番号だけを渡す
struct VS_OUT { uint vertex_id : VERTEXID; };

// GS → PS
struct GS_OUT
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD;
    uint   type     : TEXCOORD1;  // 0 = Point（テクスチャを使わない）, 1 = Quad
};

#define PI           3.14159265358979f
#define STATE_DEAD   0
#define STATE_ALIVE  1

// 整数を散らすハッシュ（Wang Hash）。近い入力からも、かけ離れた値を返す
uint wangHash(uint s)
{
    s = (s ^ 61u) ^ (s >> 16u);
    s *= 9u;
    s = s ^ (s >> 4u);
    s *= 0x27d4eb2du;
    s = s ^ (s >> 15u);
    return s;
}

// 種から [0, 1) の乱数を作る（下位 24 ビットを使う）
float randUint(uint s)
{
    return (wangHash(s) & 0x00FFFFFFu) / 16777216.0f;
}

// ベクトルをクォータニオンで回す
float3 RotateByQuat(float3 v, float4 q)
{
    return v + 2.0f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

// 粒子1個を生む（位置・速度・寿命・大きさ・最初のコマを決める）
// salt は生まれ変わりごとに変える追加の種。継続放出で同じ粒子番号を使い回すときは、前回の p.seed を渡して数珠つなぎにする
// 初期化（initialize_particle_cs）では 0 を渡す
void spawn(in uint id, inout particle p, in uint salt)
{
    // 処理フロー:
    // 1. 粒子ごとの種と乱数を作る
    // 2. 前のフレームと今のフレームの間で、エミッタの位置と向きを決める
    // 3. 生まれる位置（刃に沿う線分か、円盤）
    // 4. 速度（円錐の中のどこかへ）
    // 5. 寿命・大きさ・最初のコマ

    // ---------------------------------------------------------
    // 1. 種と乱数
    // ---------------------------------------------------------

    // 種は3つの要素から作る。どれが欠けても同じ絵の繰り返しが起きる
    //   id            … 同じエミッタの粒子どうしを散らす
    //   instance_seed … 同じプレハブから出した VFX どうしを散らす（ないと、並んで出たスパーク2つが寸分違わない）
    //   salt          … 同じ粒子番号の生まれ変わりどうしを散らす（ないと、継続放出のエミッタが完全に周期的になる）
    // 足す前に wangHash で散らす。そのまま足すと (インスタンス1・粒子5) と (インスタンス2・粒子4) が同じ値になる
    p.seed = wangHash(id + wangHash(instance_seed + salt));

    float r_rand     = randUint(p.seed +  1u);
    float a_rand     = randUint(p.seed +  2u);
    float speed_rand = randUint(p.seed +  3u);
    float life_rand  = randUint(p.seed +  4u);
    float size_rand  = randUint(p.seed +  5u);
    float chip_rand  = randUint(p.seed +  6u);
    float ang_rand   = randUint(p.seed +  7u);
    float y_rand     = randUint(p.seed +  8u);

    // ---------------------------------------------------------
    // 2. エミッタの位置と向き
    // ---------------------------------------------------------

    // 前のフレームと今のフレームの間（s = 0〜1）で生む
    // 1フレームに生まれる粒子が1本の線に固まらず、剣が動いた軌道の上に続けて並ぶ（速く振っても櫛にならない）
    float  s = randUint(p.seed + 15u);
    float3 emitPos = lerp(prev_world_position, world_position, s);
    float4 q0 = prev_emit_rotation;
    float4 q1 = emit_rotation;
    if (dot(q0, q1) < 0.0f) q1 = -q1;                 // 近い方の回り方を選ぶ（隣のフレームなので、正規化した線形補間で足りる）
    float4 emitRot = normalize(lerp(q0, q1, s) + float4(0, 0, 0, 1e-6f));

    // ---------------------------------------------------------
    // 3. 生まれる位置
    // ---------------------------------------------------------

    float angle = a_rand * 2.0f * PI;

    if (emit_shape > 0.5f)
    {
        // 線分: 剣の刃に沿って撒く
        //   根元（emit_local_offset）から先（+ emit_line_end）の間に均一に置き、emit_radius を刃のまわりの筒の太さにする
        //   最後にエミッタの向き（ボーンの向き）で回すので、剣を振っても刃に張り付いたまま
        float  tparam = randUint(p.seed + 14u);
        float3 baseL  = emit_local_offset.xyz;
        float3 tipL   = emit_local_offset.xyz + emit_line_end;
        float3 lineP  = lerp(baseL, tipL, tparam);

        // 刃の軸に直交する2本の向きを作り、その円周の上に太さの分だけ散らす
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
        // 円盤: 内側の半径から外側の半径の間に、面積で均一に置く（emit_stretch で楕円にする）
        float r = sqrt(lerp(emit_radius.x * emit_radius.x,
                            emit_radius.y * emit_radius.y, r_rand));
        float3 offset;
        offset.x = r * cos(angle) * emit_stretch.x;
        offset.z = r * sin(angle) * emit_stretch.y;
        offset.y = emit_local_offset.w * (y_rand * 2.0f - 1.0f); // w は上下に散らす幅

        p.position = emitPos + emit_local_offset.xyz + offset;
    }

    // ---------------------------------------------------------
    // 4. 速度
    // ---------------------------------------------------------

    // 上（+Y）を軸に、cone_angle の範囲の円錐の中へ飛ばし、エミッタの向きで回す
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
    // mass はどこからも読まない（重力は質量によらない）
    // 効果全体で粒子のバッファを1本にまとめるとき、この4バイトを「どのエミッタの粒子か」に使えるよう残している
    p.mass          = 1.0f;
    p.angle         = PI * ang_rand;
    p.color         = float4(1, 1, 1, 1);

    // ---------------------------------------------------------
    // 5. 寿命・大きさ・最初のコマ
    // ---------------------------------------------------------

    // 継続放出は、生まれるまでの待ち時間を負の年齢として持たせる
    p.lifespan = lerp(lifespan.x, lifespan.y, life_rand);
    p.age      = (emit_mode == 1)
                 ? 0.0f
                 : -lerp(spawn_delay.x, spawn_delay.y, randUint(p.seed + 12u));
    p.state    = STATE_ALIVE;

    // 大きさ。size.x が生まれたとき、size.y が消えるとき（particle_scale をここで掛けておく）
    //   size_random はばらつき。1 なら 0〜指定値の一様な乱数、0 なら指定値ちょうど
    //   衝撃波のように毎回同じ大きさで出したい効果は 0 にする
    //   生まれたときと消えるときで別の乱数を引くので、1 のままだと大きく生まれて大きく消えることもあれば、その逆もある
    float birth_rand = lerp(1.0f - size_random, 1.0f, size_rand);
    float death_rand = lerp(1.0f - size_random, 1.0f, randUint(p.seed + 13u));
    p.size.x = emit_size.x * birth_rand * particle_scale.x;
    p.size.y = emit_size.y * death_rand * particle_scale.y;

    // スプライトシートの最初のコマ
    int total = (int)(sprite_sheet_grid.x * sprite_sheet_grid.y);
    if (sprite_anim_mode == 1)
        p.chip = 0;                              // 寿命に合わせて進める: 最初のコマから
    else
        p.chip = (uint)(chip_rand * (float)total); // ランダム: どれか1コマのまま
}

// 0〜1 の t を曲線に通す
//   mode: 0 = 線形, 1 = Ease-In（t^k）, 2 = Ease-Out（1-(1-t)^k）, 3 = Ease-InOut
//   power（k）: 曲線の急さ。1 で線形と同じ
// インスペクタのグラフ（GPUParticleComponentMeta.cpp の EvalEase）と同じ式。片方を変えたらもう片方も合わせる
float ApplyEase(float t, int mode, float power)
{
    t       = saturate(t);
    float k = max(power, 1e-3f);

    // return は最後の1回だけにする。途中で return すると FXC が「戻り値が未初期化かも」（X4000）と判断し、
    // それを使う SV_POSITION にまで警告 X4715 が連鎖する
    float result = t;
    if (mode == 1)
        result = pow(t, k);
    else if (mode == 2)
        result = 1.0f - pow(1.0f - t, k);
    else if (mode == 3)
        result = (t < 0.5f) ? 0.5f * pow(2.0f * t, k)
                            : 1.0f - 0.5f * pow(2.0f * (1.0f - t), k);
    return result;
}

// 生まれてから duration 秒で 0 → 1 になるフェード
float fade_in(float duration, float age, float exponent)
{
    return pow(smoothstep(0.0f, 1.0f, age / duration), exponent);
}
// 寿命の残りが duration 秒を切ると 1 → 0 になるフェード
float fade_out(float duration, float age, float ls, float exponent)
{
    return pow(smoothstep(0.0f, 1.0f, (ls - age) / duration), exponent);
}

#endif // PARTICLE_HLSLI
