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
    // ★ p.type = type は廃止。GS/PS は CbParticleStatic.particle_type を直接読む。

    if (emit_mode == 1)  // バーストモード
    {
        if (burst_trigger > 0.0f && id < (uint)burst_count)
        {
            if (p.state == STATE_DEAD || p.age > p.lifespan)
            {
                spawn(id, p);
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
        p.age += delta_time;

        if (p.age > p.lifespan)
        {
            spawn(id, p);
            p.state = STATE_ALIVE;
        }

        if (p.age > 0.0f)
        {
            UpdateParticle(p);
        }
    }

    particle_buffer[id] = p;
}

void UpdateParticle(inout particle p)
{
    float3 force = 0.0f;

    // 重力
    force += p.mass * float3(0.0f, gravity, 0.0f);

    // 乱流ノイズ (p.seed で粒子ごとに独立した UV オフセットを与える)
    float seed_f  = (float)(p.seed & 0xFFFFu) / 65535.0f;
    float3 uvw    = p.position * noise_scale
                  + float3(seed_f * 0.37f, time * 0.1f, seed_f * 0.61f + 0.2f);
    float3 noise  = noise_3d.SampleLevel(sampler_linear, uvw, 0).xyz;
    float3 noiseDir = noise * 2.0f - 1.0f;
    p.velocity += noiseDir * noise_strength * delta_time;

    // 力 → 加速度 → 速度更新
    p.velocity += force / p.mass * delta_time;

    // 速度クランプ
    float spd = length(p.velocity);
    const float max_speed = 10.0f;
    if (spd > max_speed)
        p.velocity = normalize(p.velocity) * max_speed;

    // 位置更新
    p.position += p.velocity * delta_time;

    // 回転
    p.angle += p.angular_speed * delta_time;

    // ---- 色更新 ----
    // ★ p.type は廃止。CbParticleStatic.particle_type を使う。
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
            p.color     = pow(p.color, 2.3f);
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
