// ===========================================================================
// 概要: GPU パーティクルの毎フレームの更新。粒子を生み、動かし、色と大きさを決め、寿命で消す
//
// 入力: 粒子のバッファ・エミッタの設定（cbuffer）・色のランプ・3D ノイズ
// 出力: 更新した粒子のバッファ
//
// 備考: 粒子の種類（particle_type）は全粒子で同じなので、粒子ごとに持たず cbuffer から読む（1粒子 4 バイトの節約）
// ===========================================================================
#include "Shader/Compute/GPUParticle/particle.hlsli"

void UpdateParticle(inout particle p);

RWStructuredBuffer<particle> particle_buffer : register(REG_UAV(SLOT_UAV_PARTICLE_BUF));

SamplerState   sampler_linear     : register(REG_SMP(SLOT_SMP_LINEAR));
Texture2D      color_ramp         : register(REG_SRV(SLOT_SRV_MAT_TEX1)); // 縦方向が年齢（上 = 生まれたとき）の色
Texture3D<float4> noise_3d        : register(REG_SRV(SLOT_SRV_MAT_TEX2)); // 乱流の向き（0〜1 を -1〜1 に直して使う）

[numthreads(PARTICLE_CS_THREADS, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    // 処理フロー:
    // 1. エミッタが動いた分、生きている粒子を付いていかせる
    // 2. バースト: 撃たれたら死んでいる粒子を生み、寿命まで動かす
    // 3. 継続: 寿命が尽きた粒子を生み直し（止めてあれば待たせ）、動かす

    uint id = dtid.x;
    if (id >= (uint)max_count) return;

    particle p = particle_buffer[id];

    // ---------------------------------------------------------
    // 1. エミッタへの追従
    // ---------------------------------------------------------

    // 前のフレームから生きている粒子を、エミッタが動いた分 × 追従率だけずらす
    //   この後で生まれる粒子は spawn() が今のフレームの軌道の上に置くので、生まれる前にずらしておく
    //   移動量に delta_time を掛けないので、スロー中の粒子もエミッタの動きにそのまま付いていく
    if (follow_ratio > 0.0f && p.state == STATE_ALIVE)
        p.position += (world_position - prev_world_position) * follow_ratio;

    // ---------------------------------------------------------
    // 2. バースト
    // ---------------------------------------------------------

    if (emit_mode == 1)
    {
        if (burst_trigger > 0.0f && id < (uint)burst_count)
        {
            if (p.state == STATE_DEAD || p.age > p.lifespan)
            {
                // 前の命の種を salt に渡して数珠つなぎにする
                // ないと、同じエミッタで2回目のバーストを撃ったとき、1回目とまったく同じ粒子が生まれる
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
    else
    {
        // ---------------------------------------------------------
        // 3. 継続
        // ---------------------------------------------------------

        bool wasAlive = (p.state == STATE_ALIVE);

        // 生きている粒子は、放出を止めても寿命まで年を取り、尾を引いて自然に消える
        // 死んで待っている粒子は、放出中だけ年を取る（止めている間はずらした年齢を保ち、再開したときに一斉に生まれないようにする）
        if (wasAlive || emit_enabled > 0.5f)
            p.age += delta_time;

        if (p.age > p.lifespan)
        {
            if (emit_enabled > 0.5f)
            {
                // 放出中: 生み直す。前の命の種を salt に渡して数珠つなぎにする
                // ないと、粒子番号5番は生まれ変わるたびに同じ軌道を描き、エミッタ全体が同じ模様を繰り返す
                const uint salt = p.seed;
                spawn(id, p, salt);
                p.state = STATE_ALIVE;
            }
            else
            {
                // 止めてある: 死んで待つ。年齢をずらし直し、次に放出を始めたときも順に生まれるようにする（連続攻撃でも流れが続く）
                p.state   = STATE_DEAD;
                p.color.a = 0.0f;
                p.age     = ((float)id / (float)max_count) * p.lifespan;
            }
        }

        // 年齢が負のうちは、生まれるまでの待ち時間なので動かさない
        if (p.state == STATE_ALIVE && p.age > 0.0f)
        {
            UpdateParticle(p);
        }
    }

    particle_buffer[id] = p;
}

// 生きている粒子を1フレーム分進める
void UpdateParticle(inout particle p)
{
    // 処理フロー:
    // 1. 速度に力を足す（重力・乱流・引力と渦・空気抵抗）。速すぎれば抑える
    // 2. 位置と回転を進める
    // 3. 色と不透明度を決める
    // 4. スプライトシートのコマを進める

    // ---------------------------------------------------------
    // 1. 力
    // ---------------------------------------------------------

    // 重力。等加速度なので質量によらない
    p.velocity += float3(0.0f, gravity, 0.0f) * delta_time;

    // 乱流。p.seed で粒子ごとに読む場所をずらす
    // 強さが 0 のエミッタまで 3D テクスチャを読むと無駄なので、使うときだけ読む
    if (noise_strength > 0.0f)
    {
        float seed_f  = (float)(p.seed & 0xFFFFu) / 65535.0f;
        float3 uvw    = p.position * noise_scale
                      + float3(seed_f * 0.37f, time * 0.1f, seed_f * 0.61f + 0.2f);
        float3 noise  = noise_3d.SampleLevel(sampler_linear, uvw, 0).xyz;
        float3 noiseDir = noise * 2.0f - 1.0f;
        p.velocity += noiseDir * noise_strength * delta_time;
    }

    // 引力・斥力・渦。force_flags の 1 ビット目で有効、2 ビット目で中心をエミッタからの相対位置にする
    if (force_flags & 1u)
    {
        float3 center = (force_flags & 2u) ? (world_position + attractor_pos.xyz)
                                             : attractor_pos.xyz;
        float3 toCenter = center - p.position;
        float dist = length(toCenter) + 1e-4f;
        float3 dir = toCenter / dist;

        // 中心から離れるほど弱める。attractor_radius が 0 なら弱めない。0 より大きければ、その半径でちょうど 0 になる
        float falloff = 1.0f;
        if (attractor_radius > 0.0f)
        {
            float nd = saturate(dist / attractor_radius);
            falloff = (1.0f - nd) * (1.0f - nd);
        }

        // attractor_pos.w が強さ。正なら中心へ吸い込み、負なら外へ弾く
        p.velocity += dir * attractor_pos.w * falloff * delta_time;

        // 渦。中心を通る縦の軸のまわりを回す（魔法陣に巻き込むような動き）
        if (abs(vortex_strength) > 1e-5f)
        {
            float3 t = cross(float3(0.0f, 1.0f, 0.0f), toCenter);
            float tl = length(t);
            if (tl > 1e-4f)
                p.velocity += (t / tl) * vortex_strength * falloff * delta_time;
        }
    }

    // 空気抵抗。引力で集めた粒子を、中心で止めるのに使う
    if (drag > 0.0f)
        p.velocity *= saturate(1.0f - drag * delta_time);


    // 速さの上限。max_speed が 0 以下なら上限なし（attractor_radius と同じ決まり）
    if (max_speed > 0.0f)
    {
        float spd = length(p.velocity);
        if (spd > max_speed)
            p.velocity = p.velocity * (max_speed / spd);
    }

    // ---------------------------------------------------------
    // 2. 位置と回転
    // ---------------------------------------------------------

    p.position += p.velocity * delta_time;

    p.angle += p.angular_speed * delta_time;

    // ---------------------------------------------------------
    // 3. 色と不透明度
    // ---------------------------------------------------------

    // base_alpha は手動の色のアルファ。インスペクタでアルファも変えられるので、下のフェードに掛ける
    float base_alpha = 1.0f;
    if (particle_type == 0)
    {
        p.color.rgb = manual_color.rgb;
        base_alpha  = manual_color.a;
    }
    else
    {
        if (color_mode == 1)
        {
            p.color     = color_ramp.SampleLevel(sampler_linear,
                                float2(0.5f, p.age / p.lifespan), 0);
            // sRGB からリニアへ。手動の色（C++ の SrgbToLinear）と同じ 2.2 乗にそろえる（ToneMapPS が最後に 1/2.2 乗で戻す）
            // アルファは下でフェードに上書きされる。負の値は pow が NaN を返すので saturate で挟む（警告 X3571 も消える）
            p.color.rgb = pow(saturate(p.color.rgb), 2.2f);
        }
        else
        {
            p.color.rgb = manual_color.rgb;
            base_alpha  = manual_color.a;
        }
    }

    // 光の強さ。描画先は HDR なので、1 を超えた分が光ってブルームがかかる
    // 不透明度は 1 を超えられないので、アルファには掛けない
    p.color.rgb *= intensity;

    float alpha  = base_alpha
                 * fade_in(fade_duration.x, p.age, 1.0f)
                 * fade_out(fade_duration.y, p.age, p.lifespan, 1.0f);
    p.color.a = alpha;

    // ---------------------------------------------------------
    // 4. スプライトシートのコマ
    // ---------------------------------------------------------

    // 寿命に合わせて最初のコマから最後のコマへ進める
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
