#include "Shader/Compute/GPUParticle/particle.hlsli"

void UpdateParticle(inout particle p);

RWStructuredBuffer<particle> particle_buffer : register(REG_UAV(SLOT_UAV_PARTICLE_BUF));

SamplerState   sampler_linear     : register(REG_SMP(SLOT_SMP_LINEAR));
Texture2D      color_ramp         : register(REG_SRV(SLOT_SRV_MAT_TEX1));
Texture3D<float4> noise_3d        : register(REG_SRV(SLOT_SRV_MAT_TEX2));

[numthreads(NUMTHREADS_X, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint id = dtid.x;
    if (id >= (uint)max_count) return;

    particle p = particle_buffer[id];
    // 種別は粒子ごとに持たず、GS/PS が CbParticleStatic.particle_type を直接読む
    // （全粒子で同一の値のため、1粒子あたり4バイトを節約する）。

    if (emit_mode == 1)  // バーストモード
    {
        if (burst_trigger > 0.0f && id < (uint)burst_count)
        {
            if (p.state == STATE_DEAD || p.age > p.lifespan)
            {
                // 前の人生の種を salt に渡して数珠つなぎにする。
                //   これが無いと、同じエミッタで2回目のバーストを撃ったとき
                //   1回目とまったく同じ粒子が生まれる。
                const uint salt = p.seed;
                spawn(id, p, salt);
                p.state = STATE_ALIVE;
            }
        }

        if (p.state == STATE_ALIVE)
        {
            p.age += delta_time;
            if (p.age > p.lifespan)
            {
                p.state   = STATE_DEAD;
                p.color.a = 0.0f;
            }
            else
            {
                UpdateParticle(p);
            }
        }
    }
    else  // 継続放出モード
    {
        bool wasAlive = (p.state == STATE_ALIVE);

        // 生存粒子は常に加齢（ゲートOFFでも寿命まで生きて尾を引いて自然に消える）。
        // 死亡待機粒子はゲートON中のみ加齢（OFF中は staggered な位相を凍結し、
        // ゲート開放時に一斉ではなく順次スポーンさせる＝定常流を保証）。
        if (wasAlive || emit_enabled > 0.5f)
            p.age += delta_time;

        if (p.age > p.lifespan)
        {
            if (emit_enabled > 0.5f)
            {
                // ゲートON: 軌道上(サブフレーム補間)にスポーン。
                //   前の人生の種を salt に渡して数珠つなぎにする。これが無いと
                //   粒子番号5番は生まれ変わるたびに同じ軌道を描き、エミッタ全体が
                //   完全に周期的な（同じ模様を繰り返す）見た目になる。
                const uint salt = p.seed;
                spawn(id, p, salt);
                p.state = STATE_ALIVE;
            }
            else
            {
                // ゲートOFF: 一斉スポーン防止のため age を staggered に戻して死亡待機。
                // これで次にゲートが開いたときも順次スポーンになる（連続攻撃でも定常）。
                p.state   = STATE_DEAD;
                p.color.a = 0.0f;
                p.age     = ((float)id / (float)max_count) * p.lifespan;
            }
        }

        if (p.state == STATE_ALIVE && p.age > 0.0f)
        {
            UpdateParticle(p);
        }
    }

    particle_buffer[id] = p;
}

void UpdateParticle(inout particle p)
{
    // 重力。等加速度なので質量には依らない
    // （以前は force に p.mass を掛けたあと p.mass で割っていたが、常に相殺していた）。
    p.velocity += float3(0.0f, gravity, 0.0f) * delta_time;

    // 乱流ノイズ (p.seed で粒子ごとに独立した UV オフセットを与える)
    //   強度0のエミッタまで3Dテクスチャを引くと丸損なので、使うときだけサンプルする。
    if (noise_strength > 0.0f)
    {
        float seed_f  = (float)(p.seed & 0xFFFFu) / 65535.0f;
        float3 uvw    = p.position * noise_scale
                      + float3(seed_f * 0.37f, time * 0.1f, seed_f * 0.61f + 0.2f);
        float3 noise  = noise_3d.SampleLevel(sampler_linear, uvw, 0).xyz;
        float3 noiseDir = noise * 2.0f - 1.0f;
        p.velocity += noiseDir * noise_strength * delta_time;
    }

    // ---- Force Field (引力/斥力/渦) ----
    if (force_flags & 1u)
    {
        float3 center = (force_flags & 2u) ? (world_position + attractor_pos.xyz)
                                             : attractor_pos.xyz;
        float3 toCenter = center - p.position;
        float dist = length(toCenter) + 1e-4f;
        float3 dir = toCenter / dist;

        // 半径フォールオフ (0=無限。>0 なら端で滑らかに 0 へ)
        float falloff = 1.0f;
        if (attractor_radius > 0.0f)
        {
            float nd = saturate(dist / attractor_radius);
            falloff = (1.0f - nd) * (1.0f - nd);
        }

        // 引力/斥力: w(=強さ)>0 で中心へ吸い込み、<0 で外へ弾く
        p.velocity += dir * attractor_pos.w * falloff * delta_time;

        // 渦: 中心軸(Y)まわりの接線方向（魔法陣に巻き込む旋回）
        if (abs(vortex_strength) > 1e-5f)
        {
            float3 t = cross(float3(0.0f, 1.0f, 0.0f), toCenter);
            float tl = length(t);
            if (tl > 1e-4f)
                p.velocity += (t / tl) * vortex_strength * falloff * delta_time;
        }
    }

    // ---- ドラッグ (空気抵抗。引力で集めた粒子を中心に沈めるのに有効) ----
    if (drag > 0.0f)
        p.velocity *= saturate(1.0f - drag * delta_time);


    // 速度クランプ (max_speed <= 0 は「制限なし」。attractor_radius と同じ約束)
    if (max_speed > 0.0f)
    {
        float spd = length(p.velocity);
        if (spd > max_speed)
            p.velocity = p.velocity * (max_speed / spd); // normalize + 乗算の分を1回に
    }

    // 位置更新
    p.position += p.velocity * delta_time;

    // 回転
    p.angle += p.angular_speed * delta_time;

    // ---- 色更新 ----
    // 種別は定数バッファ側の particle_type を参照する
    if (particle_type == 0)
    {
        p.color.rgb = manual_color.rgb;
    }
    else
    {
        if (color_mode == 1)
        {
            p.color     = color_ramp.SampleLevel(sampler_linear,
                                float2(0.5f, p.age / p.lifespan), 0);
            // ガンマ補正。負値が来ると pow が NaN を返すので saturate で挟む
            // （テクスチャ由来なので通常 0〜1 だが、警告 X3571 を消す意味も兼ねる）。
            p.color     = pow(saturate(p.color), 2.3f);
        }
        else
        {
            p.color.rgb = manual_color.rgb;
        }
    }

    float alpha  = fade_in(fade_duration.x, p.age, 1.0f)
                 * fade_out(fade_duration.y, p.age, p.lifespan, 1.0f);
    p.color.a = alpha;

    // ---- スプライトアニメーション ----
    if (sprite_anim_mode == 1)
    {
        uint total = sprite_sheet_grid.x * sprite_sheet_grid.y;
        if (total > 1)
        {
            float t  = saturate(p.age / p.lifespan);
            p.chip   = (uint)(t * (float)total);
            if (p.chip >= total) p.chip = total - 1;
        }
    }
}
