#include "Shader/Compute/GPUParticle/particle.hlsli"

RWStructuredBuffer<particle> particle_buffer : register(REG_UAV(SLOT_UAV_PARTICLE_BUF));

[numthreads(NUMTHREADS_X, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint id = dtid.x;
    if (id >= (uint)max_count) return;  // 旧: max_particle_count

    particle p = particle_buffer[id];
    spawn(id, p);

    if (emit_mode == 1)  // 旧: emission_mode → emit_mode
    {
        // バーストモード: 初期状態は死亡待機
        p.state   = STATE_DEAD;
        p.color.a = 0.0f;
        p.age     = p.lifespan + 1.0f;
    }
    else
    {
        // 継続放出モード: Pre-warm (最初から粒子が散らばった状態)
        p.state = STATE_ALIVE;

        float start_ratio = (float)id / (float)max_count;
        p.age = start_ratio * p.lifespan;

        // Pre-warm: age 分だけ位置・速度を進めておく
        p.position += p.velocity * p.age;

        float3 gVec = float3(0.0f, gravity, 0.0f);
        p.position += 0.5f * gVec * p.age * p.age;
        p.velocity += gVec * p.age;
    }

    particle_buffer[id] = p;
}
