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

    // 死亡待機の粒子は描かない。継続モードの死亡待機は一斉スポーン防止のため
    //   age を 0〜寿命の間にずらして置いているので、age だけでは弾けない
    //   （アルファ 0 で見えないが、四角形を作ってラスタライズする分の負荷が残っていた）。
    if (p.state == STATE_DEAD) return;
    if (p.age <= 0.0f || p.age >= p.lifespan) return;

    // アスペクト比はプロジェクション行列から導出する（解像度変更に追従させるため）
    // gProjection[0][0] = cot(fovY/2) / aspect, gProjection[1][1] = cot(fovY/2)
    // よって aspect = _m11 / _m00
    float aspect = gProjection._m11 / gProjection._m00;

    float age_ratio = saturate(p.age / p.lifespan);
    float size      = lerp(p.size.x, p.size.y, age_ratio);

    float2 stretch_scale  = float2(1.0f, 1.0f);
    float  rotation_angle = p.angle;

    // 速度方向への引き伸ばしは画面上で計算する仕組みなので、ビルボードのときだけ使う
    if (orient_mode == 0 && velocity_stretch > 0.0f)
    {
        float3 view_vel = mul(float4(p.velocity, 0.0f), viewProjection).xyz;

        // クリップ空間は x と y で1単位のピクセル数が違う（16:9 なら x が 1.78 倍）。
        //   四角形を回す空間は下で size * aspect を掛けるのでピクセル上で縦横が等しい。
        //   速度も x に aspect を掛けて同じ空間へそろえないと、斜めの火花の向きと
        //   伸びる量がずれる（45° に飛ぶ粒子が約 60° を向いて伸びていた）。
        float2 screen_vel = float2(view_vel.x * aspect, view_vel.y);
        float  spd        = length(screen_vel);
        if (spd > 0.001f)
        {
            rotation_angle   = atan2(screen_vel.y, screen_vel.x) - PI / 2.0f;
            stretch_scale.y  = 1.0f + spd * velocity_stretch;
            stretch_scale.x  = max(0.1f, 1.0f - spd * velocity_stretch * 0.1f);
        }
    }

    // ---- 平面モードの向き（四角形を広げる2本の軸） ----
    //   1: ワールド水平。X と Z に広げるので、常に地面と平行に寝る
    //   2: エミッタの向き。水平面をエミッタの回転で傾ける
    //      （毎フレーム今の回転を使うので、追従させると出た後も一緒に回る）
    float3 axis_u = float3(1.0f, 0.0f, 0.0f);
    float3 axis_v = float3(0.0f, 0.0f, 1.0f);
    if (orient_mode == 2)
    {
        axis_u = RotateByQuat(axis_u, emit_rotation);
        axis_v = RotateByQuat(axis_v, emit_rotation);
    }

    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        GS_OUT elem;

        float2 raw = corners[i] * stretch_scale;

        float c = cos(rotation_angle), s = sin(rotation_angle);
        float2 rot;
        rot.x = raw.x * c - raw.y * s;
        rot.y = raw.x * s + raw.y * c;

        if (orient_mode == 0)
        {
            // ビルボード: 中心を画面へ変換してから、画面上で四隅をずらす（常にカメラを向く）
            elem.position     = mul(float4(p.position, 1.0f), viewProjection);
            elem.position.xy += rot * float2(size, size * aspect);
        }
        else
        {
            // 平面: ワールドで四隅をずらしてから画面へ変換する（空間に固定され、斜めからは楕円に見える）。
            //   size はそのままメートル（中心から端までの長さ）になる
            float3 world = p.position + (axis_u * rot.x + axis_v * rot.y) * size;
            elem.position = mul(float4(world, 1.0f), viewProjection);
        }
        elem.color        = p.color;

        // 種別は全粒子共通なので、粒子構造体ではなく定数バッファから読む
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
