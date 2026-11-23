// ===========================================================================
// 概要: GPU パーティクルで C++ と HLSL が共有する構造体（粒子1個・エミッターの定数バッファ）
//
// 責務:
//   - GpuParticle（StructuredBuffer の1要素）を定義する
//   - CbParticleStatic（設定の値）と CbParticleDynamic（毎フレームの値）を定義する
//   - C++ 側で大きさを static_assert する
//
// 備考: ShaderShared.h と同じく、C++ では GpuData の構造体、HLSL（particle.hlsli）では cbuffer になる
//       2つの定数バッファは隣のスロット（b6, b7）なので、SetConstantBuffers の1回でバインドできる
//       HLSL からも読むので、#pragma once でなくインクルードガードを使う
// ===========================================================================

#ifndef PARTICLE_SHARED_H
#define PARTICLE_SHARED_H

#include "Engine/Graphics/Shader/ShaderResources.h"

// コンピュートシェーダーの1グループのスレッド数。HLSL の numthreads と C++ の Dispatch の
//   グループ数計算の両方がこれを見る（片方だけ変えると末尾の粒子が更新されなくなる）
//   GPU は 32（NVIDIA）か 64（AMD）スレッドを1単位で動かすので、64 にして遊ぶスレッドをなくす
#define PARTICLE_CS_THREADS 64

#ifdef __cplusplus
#include <DirectXMath.h>
#include <cstdint>

namespace GpuData
{

    using float2   = DirectX::XMFLOAT2;
    using float3   = DirectX::XMFLOAT3;
    using float4   = DirectX::XMFLOAT4;
    using float4x4 = DirectX::XMFLOAT4X4;
    using uint     = uint32_t;
    using uint2    = DirectX::XMUINT2;

#define CB_PARTICLE_STRUCT(name, slot) struct name

#else
#define CB_PARTICLE_STRUCT(name, slot) cbuffer name : register(REG_CB(slot))
#endif

    // ==================================================================
    // GpuParticle: 粒子1個の状態（StructuredBuffer の1要素）
    //
    //   種類（Point / Quad）はエミッターの全粒子で同じなので、粒子ごとには持たず
    //   CbParticleStatic::particle_type を読む。大きさの倍率（particle_scale）は
    //   発生したときに size へ掛けてしまうので、粒子ごとの倍率も持たない
    //
    //   並び（80 バイト = 16 バイト × 5）
    //     offset  0: state, angle, angular_speed, mass
    //     offset 16: color (RGBA)
    //     offset 32: position.xyz, lifespan
    //     offset 48: velocity.xyz, age
    //     offset 64: size.xy, chip, seed
    // ==================================================================
    struct GpuParticle
    {
        int   state;         // 0 = 消えている, 1 = 生きている
        float angle;         // 今の回転（rad）
        float angular_speed; // 回転の速さ（rad/s）
        float mass;          // 未使用の予約枠（重力は質量に依らない）。spawn() は 1 を入れる

        float4 color; // 今の色（RGBA）

        float3 position; // ワールド座標（m）
        float  lifespan; // 寿命（秒）

        float3 velocity; // 速度（m/s）
        float  age;      // 発生してからの時間（秒。負なら発生を待っている）

        float2 size; // x = 発生したときの大きさ, y = 消えるときの大きさ（particle_scale を掛け済み）
        uint   chip; // スプライトシートのコマの番号
        uint   seed; // 粒子ごとの乱数の種（ノイズの動きを粒子ごとに変える）
    };

    // ==================================================================
    // CbParticleStatic: エミッターの設定の値（256 バイト。変えたときだけ送る）
    //   ParticleEmitterDesc から作る
    // ==================================================================
    CB_PARTICLE_STRUCT(CbParticleStatic, SLOT_CB_PARTICLE_STATIC)
    {
        // ---- 発生する形（offset 0〜95） ----
        float4 emit_local_offset; // offset   0: xyz = エミッターからのずれ（m）, w = Y 方向の揺らぎの幅（m）

        float2 emit_radius; // offset  16: 発生する半径の最小・最大（m）
        float2 emit_size;   // offset  24: x = 発生したときの大きさ, y = 消えるときの大きさ

        float2 cone_angle; // offset  32: 放出する角度の範囲（rad）
        float2 emit_speed; // offset  40: 初速の範囲（m/s）

        float2 emit_angular_speed; // offset  48: 回転の速さの範囲（rad/s）
        float2 lifespan;           // offset  56: 寿命の範囲（秒）

        float2 spawn_delay;   // offset  64: 発生を待つ時間の範囲（秒）
        float2 fade_duration; // offset  72: x = フェードインの時間, y = フェードアウトの時間（秒）

        float4 manual_color; // offset  80: 掛ける色（RGBA。color_mode が 0 のとき）

        // ---- 物理・ノイズ（offset 96〜127） ----
        float noise_scale;      // offset  96
        float noise_strength;   // offset 100
        float gravity;          // offset 104: 重力（負なら下向き）
        float velocity_stretch; // offset 108: 速さに応じて進む向きに伸ばす量

        float2 uv_scroll_speed; // offset 112: UV をスクロールする速さ
        float2 emit_stretch;    // offset 120: 発生する形の倍率 x = X 軸, y = Z 軸

        // ---- 見た目（offset 128〜143） ----
        float2 particle_scale;    // offset 128: 発生・消えるときの大きさの倍率（発生したときに size に掛ける）
        uint2  sprite_sheet_grid; // offset 136: スプライトシートの分割数（列, 行）

        // ---- モード（offset 144〜175） ----
        int sprite_anim_mode; // offset 144: 0 = ランダムなコマ, 1 = 寿命に合わせてコマを進める
        int render_mode;      // offset 148: 0 = 加算, 1 = 半透明
        int color_mode;       // offset 152: 0 = manual_color, 1 = カラーランプ
        int particle_type;    // offset 156: 0 = Point, 1 = Quad

        int max_count;   // offset 160: 粒子の最大数
        int emit_mode;   // offset 164: 0 = 出し続ける, 1 = バースト
        int burst_count; // offset 168: バーストで出す数

        // offset 172: 速さの上限（m/s）。0 以下なら上限なし
        //   上限をシェーダーに定数で書いてはいけない。設定から渡さないと、初速を上限より
        //   大きくしても黙って上限に抑えられ、設定が効かないように見える
        float max_speed; // offset 172

        // ---- 力場（offset 176〜207） ----
        float4 attractor_pos;    // offset 176: xyz = 中心, w = 強さ（正 = 引き寄せる, 負 = 押し出す）
        float  attractor_radius; // offset 192: 効く半径（0 なら無限）
        float  vortex_strength;  // offset 196: 中心の Y 軸まわりの渦の強さ
        float  drag;             // offset 200: 速度の減衰（1/s）
        uint   force_flags;      // offset 204: bit0 = 引力・渦を使う, bit1 = ローカル空間で計算する

        // ---- 発生する形: 線分（剣の刃に沿って出す）（offset 208〜223） ----
        float3 emit_line_end; // offset 208: 線分の終点（刃先）。emit_local_offset を始点とするずれ
        float  emit_shape;    // offset 220: 0 = 楕円の円盤, 1 = 線分（emit_rotation で刃の向きに合わせる）

        // ---- 大きさのばらつき（offset 224〜239） ----
        // offset 224: 0 なら emit_size ちょうど、1 なら 0〜emit_size の一様な乱数
        //   1枚だけ出して広げる衝撃波のように、毎回同じ大きさで出したい効果のために外から決める
        //   既定は 1
        float  size_random;  // offset 224
        float  intensity;    // offset 228: 発光の強さ（色に掛ける倍率。1 で選んだ色のまま、1 超で光る）
        int    orient_mode;  // offset 232: 0 = ビルボード, 1 = ワールド水平, 2 = エミッタの向きの平面
        int    size_ease;    // offset 236: 大きさの変わり方 0 = 線形, 1 = Ease-In, 2 = Ease-Out, 3 = Ease-InOut

        // ---- 大きさのイージング・エミッタへの追従（offset 240〜255） ----
        float  size_ease_power; // offset 240: 曲線の急さ（1 で線形と同じ）
        float  follow_ratio;    // offset 244: 生まれた後の粒子がエミッタの移動に付いていく割合（0 = その場に残る, 1 = 完全に付いていく）
        float2 _pad_static3;    // offset 248: 16 バイトの倍数にする詰め物
    };

    // ==================================================================
    // CbParticleDynamic: 毎フレーム変わる値（80 バイト。毎フレーム送る）
    //   エミッターの位置・回転、時間、バーストの合図だけを持つ
    // ==================================================================
    CB_PARTICLE_STRUCT(CbParticleDynamic, SLOT_CB_PARTICLE_DYNAMIC)
    {
        float4 emit_rotation; // offset  0: エミッターの回転（クォータニオン）

        float3 world_position; // offset 16: エミッターのワールド座標（m）
        float  time;           // offset 28: 経過時間（秒）

        float delta_time;    // offset 32: 前のフレームからの時間（秒）
        float burst_trigger; // offset 36: 0 より大きいとバーストする（GPUParticleUpdateSystem が使ったら 0 に戻す）
        float emit_enabled;  // offset 40: 出し続けるモードで、0 なら出さない（アニメーションに合わせて止める）

        // offset 44: エミッターごとの乱数の種（0 はまだ割り当てていない）
        //   粒子の番号だけを種にすると、同じプレハブから出したエフェクトはどれも全く同じ粒子の
        //   配置になるので、この値を混ぜてエミッターごとに違う乱数にする
        uint instance_seed; // offset 44

        // 前のフレームのエミッターの姿勢。この間を補間して、動いている間も軌道に沿って途切れずに出す
        float4 prev_emit_rotation;  // offset 48
        float3 prev_world_position; // offset 64
        float  _pad_dynamic2;       // offset 76
    };

// ==================================================================
// C++ だけ: 大きさの確認（理由は ShaderShared.h を参照）
// ==================================================================
#ifdef __cplusplus
    static_assert(sizeof(GpuParticle) == 80, "GpuParticle size mismatch");
    static_assert(sizeof(CbParticleStatic) == 256, "CbParticleStatic size mismatch");
    static_assert(sizeof(CbParticleDynamic) == 80, "CbParticleDynamic size mismatch");

}

#undef CB_PARTICLE_STRUCT
#else
#undef CB_PARTICLE_STRUCT
#endif

#endif // PARTICLE_SHARED_H