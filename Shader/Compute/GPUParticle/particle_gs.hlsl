// ===========================================================================
// 概要: GPU パーティクルのジオメトリシェーダー。粒子1個から四角形を1枚作る
//
// 入力: 粒子の番号（particle_vs から）・粒子のバッファ
// 出力: 四角形の4頂点（画面の位置・色・UV）
//
// 備考: 向きは orient_mode で決まる
//         0 … ビルボード。常にカメラを向き、速度の向きに引き伸ばせる
//         1 … ワールドの水平面に寝かせる
//         2 … エミッタの向きで傾けた平面
// ===========================================================================
#include "Shader/Compute/GPUParticle/particle.hlsli"

StructuredBuffer<particle> particle_buffer : register(REG_SRV(SLOT_SRV_PARTICLE_BUF));

[maxvertexcount(4)]
void main(point VS_OUT input[1], inout TriangleStream<GS_OUT> output)
{
    // 処理フロー:
    // 1. 描かない粒子を外す
    // 2. 大きさ・回転・引き伸ばしを決める
    // 3. 四隅を作って出す

    const float2 corners[] = {
        float2(-1.0f, -1.0f), float2(-1.0f, +1.0f),
        float2(+1.0f, -1.0f), float2(+1.0f, +1.0f),
    };
    const float2 uvs[] = {
        float2(0.0f, 1.0f), float2(0.0f, 0.0f),
        float2(1.0f, 1.0f), float2(1.0f, 0.0f),
    };

    // ---------------------------------------------------------
    // 1. 描かない粒子
    // ---------------------------------------------------------

    particle p = particle_buffer[input[0].vertex_id];

    // 死んで待っている粒子は描かない。継続放出で待っている粒子は年齢を 0〜寿命の間にずらしてあるので、
    // 年齢だけでは外せない（アルファ 0 で見えなくても、四角形を作ればラスタライズの負荷がかかる）
    if (p.state == STATE_DEAD) return;
    if (p.age <= 0.0f || p.age >= p.lifespan) return;

    // ---------------------------------------------------------
    // 2. 大きさ・回転・引き伸ばし
    // ---------------------------------------------------------

    // 画面の縦横比。解像度を変えても追いつくよう、プロジェクション行列から求める
    // _m00 = cot(fovY/2) / aspect、_m11 = cot(fovY/2) なので、aspect = _m11 / _m00
    float aspect = gProjection._m11 / gProjection._m00;

    float age_ratio = saturate(p.age / p.lifespan);

    // 生まれたときから消えるときへの大きさを、年齢の割合をイージングの曲線に通してから補間する
    // （Ease-Out なら最初に一気に広がり、最後はゆっくりになる）
    float size      = lerp(p.size.x, p.size.y, ApplyEase(age_ratio, size_ease, size_ease_power));

    float2 stretch_scale  = float2(1.0f, 1.0f);
    float  rotation_angle = p.angle;

    // 速度の向きへの引き伸ばし。画面の上で計算するので、ビルボードのときだけ使う
    if (orient_mode == 0 && velocity_stretch > 0.0f)
    {
        float3 view_vel = mul(float4(p.velocity, 0.0f), viewProjection).xyz;

        // クリップ空間は、x と y で1単位のピクセル数が違う（16:9 なら x が 1.78 倍）
        // 四角形は下で size * aspect を掛けて、ピクセルの上で縦横が等しい空間で回している
        // 速度も x に aspect を掛けて同じ空間へそろえないと、斜めの火花の向きと伸びる量がずれる
        float2 screen_vel = float2(view_vel.x * aspect, view_vel.y);
        float  spd        = length(screen_vel);
        if (spd > 0.001f)
        {
            // 四角形の縦（+Y）を速度の向きへ回し、縦に伸ばして横を少し細くする
            rotation_angle   = atan2(screen_vel.y, screen_vel.x) - PI / 2.0f;
            stretch_scale.y  = 1.0f + spd * velocity_stretch;
            stretch_scale.x  = max(0.1f, 1.0f - spd * velocity_stretch * 0.1f);
        }
    }

    // 平面のときに四角形を広げる2本の軸
    //   1: ワールドの水平。X と Z に広げるので、常に地面と平行に寝る
    //   2: エミッタの向き。水平面をエミッタの回転で傾ける（毎フレーム今の回転を使うので、追従させると出た後も一緒に回る）
    float3 axis_u = float3(1.0f, 0.0f, 0.0f);
    float3 axis_v = float3(0.0f, 0.0f, 1.0f);
    if (orient_mode == 2)
    {
        axis_u = RotateByQuat(axis_u, emit_rotation);
        axis_v = RotateByQuat(axis_v, emit_rotation);
    }

    // ---------------------------------------------------------
    // 3. 四隅
    // ---------------------------------------------------------

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
            // ビルボード: 中心を画面へ移してから、画面の上で四隅をずらす（常にカメラを向く）
            elem.position     = mul(float4(p.position, 1.0f), viewProjection);
            elem.position.xy += rot * float2(size, size * aspect);
        }
        else
        {
            // 平面: ワールドで四隅をずらしてから画面へ移す（空間に固定され、斜めからは楕円に見える）
            // size はそのまま m（中心から端までの長さ）になる
            float3 world = p.position + (axis_u * rot.x + axis_v * rot.y) * size;
            elem.position = mul(float4(world, 1.0f), viewProjection);
        }
        elem.color        = p.color;

        elem.type = (uint)particle_type;

        // スプライトシートなら、今のコマの矩形の中に UV を収める
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
