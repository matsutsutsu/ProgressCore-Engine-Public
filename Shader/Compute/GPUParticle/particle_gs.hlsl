#include "Shader/Compute/GPUParticle/particle.hlsli"

StructuredBuffer<particle> particle_buffer : register(REG_SRV(SLOT_SRV_PARTICLE_BUF));

[maxvertexcount(4)]
void main(point VS_OUT input[1], inout TriangleStream<GS_OUT> output)
{
    const float2 corners[] = {
        float2(-1.0f, -1.0f), float2(-1.0f, +1.0f),
        float2(+1.0f, -1.0f), float2(+1.0f, +1.0f),
    };
    const float2 uvs[] = {
        float2(0.0f, 1.0f), float2(0.0f, 0.0f),
        float2(1.0f, 1.0f), float2(1.0f, 0.0f),
    };

    particle p = particle_buffer[input[0].vertex_id];

    if (p.age <= 0.0f || p.age >= p.lifespan) return;

    // ★ アスペクト比をプロジェクション行列から導出（ハードコード廃止）
    // gProjection[0][0] = cot(fovY/2) / aspect, gProjection[1][1] = cot(fovY/2)
    // よって aspect = _m11 / _m00
    float aspect = gProjection._m11 / gProjection._m00;

    float age_ratio = saturate(p.age / p.lifespan);
    float size      = lerp(p.size.x, p.size.y, age_ratio);

    float2 stretch_scale  = float2(1.0f, 1.0f);
    float  rotation_angle = p.angle;

    if (velocity_stretch > 0.0f)
    {
        float3 view_vel = mul(float4(p.velocity, 0.0f), viewProjection).xyz;
        float  spd      = length(view_vel.xy);
        if (spd > 0.001f)
        {
            rotation_angle   = atan2(view_vel.y, view_vel.x) - PI / 2.0f;
            stretch_scale.y  = 1.0f + spd * velocity_stretch;
            stretch_scale.x  = max(0.1f, 1.0f - spd * velocity_stretch * 0.1f);
        }
    }

    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        GS_OUT elem;
        elem.position = mul(float4(p.position, 1.0f), viewProjection);

        float2 raw = corners[i] * stretch_scale;

        float c = cos(rotation_angle), s = sin(rotation_angle);
        float2 rot;
        rot.x = raw.x * c - raw.y * s;
        rot.y = raw.x * s + raw.y * c;

        elem.position.xy += rot * float2(size, size * aspect);
        elem.color        = p.color;

        // ★ type は全粒子共通 → CbParticleStatic.particle_type から読む (per-particle フィールド廃止)
        elem.type = (uint)particle_type;

        uint2 grid = sprite_sheet_grid;
        if (grid.x * grid.y > 1)
        {
            float2 grid_size = 1.0f / (float2)grid;
            uint   x = p.chip % grid.x;
            uint   y = p.chip / grid.x;
            elem.texcoord = uvs[i] * grid_size + grid_size * uint2(x, y);
        }
        else
        {
            elem.texcoord = uvs[i];
        }

        output.Append(elem);
    }
    output.RestartStrip();
}
