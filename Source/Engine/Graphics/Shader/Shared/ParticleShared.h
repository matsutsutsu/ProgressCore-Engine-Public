// Engine/Graphics/Shader/Particle/ParticleShared.h
//
// ==================================================================
// 【パーティクル専用 C++/HLSL 共有ヘッダー】
//
// ShaderShared.h と同じ仕組みでパーティクル固有の構造体を定義する。
// ShaderShared.h にはエンジン全体で使う構造体のみを置き、
// パーティクル固有のものはここに集約することで、将来の肥大化を防ぐ。
//
// 使い方:
//   C++  : #include "...Particle/ParticleShared.h"  → GpuData::GpuParticle etc.
//   HLSL : particle.hlsli が include する             → GpuParticle, CbParticleStatic etc.
//
// CbParticleStatic と CbParticleDynamic は連続スロット (b13, b14) を使う。
// SetConstantBuffers(SLOT_CB_PARTICLE_STATIC, 2, cbs) で一括バインド可能。
// ==================================================================

#ifndef PARTICLE_SHARED_H
#define PARTICLE_SHARED_H

#include "Engine/Graphics/Shader/ShaderResources.h"

#ifdef __cplusplus
#include <DirectXMath.h>
#include <cstdint>

namespace GpuData {

    using float2 = DirectX::XMFLOAT2;
    using float3 = DirectX::XMFLOAT3;
    using float4 = DirectX::XMFLOAT4;
    using float4x4 = DirectX::XMFLOAT4X4;
    using uint = uint32_t;
    using uint2 = DirectX::XMUINT2;

#define CB_PARTICLE_STRUCT(name, slot) struct name

#else
#define CB_PARTICLE_STRUCT(name, slot) cbuffer name : register(REG_CB(slot))
#endif

// ==================================================================
// GpuParticle — StructuredBuffer の1要素（粒子1個の状態）
//
// 【設計方針】
//   - type, scale を削除 (type は全粒子共通 → CbParticleStatic.particle_type で読む)
//   - seed を追加 (per-particle 安定乱数シード → ノイズ差別化に使用)
//   - angular_speed / mass を row 1 に集約 (物理パラメータをまとめる)
//   - サイズ計算: particle_scale はスポーン時に size へベイクするため
//     per-particle の scale フィールドは不要
//
// 【レイアウト (80 bytes, 5 rows × 16B)】
//   row 1 (offset  0): state, angle, angular_speed, mass
//   row 2 (offset 16): color (RGBA)
//   row 3 (offset 32): position.xyz, lifespan
//   row 4 (offset 48): velocity.xyz, age
//   row 5 (offset 64): size.xy, chip, seed
// ==================================================================
struct GpuParticle
{
    int    state;           // 0:dead 1:alive
    float  angle;           // 現在の回転角度 (rad)
    float  angular_speed;   // 回転速度 (rad/s)
    float  mass;            // 質量 (重力・ノイズの影響係数)

    float4 color;           // 現在のカラー (RGBA)

    float3 position;        // ワールド座標
    float  lifespan;        // 最大寿命 (秒)

    float3 velocity;        // 移動速度ベクトル
    float  age;             // 経過時間 (秒、負=発生前待機中)

    float2 size;            // x:誕生時サイズ y:消滅時サイズ (particle_scale ベイク済み)
    uint   chip;            // スプライトシートのコマ番号
    uint   seed;            // per-particle 乱数シード (ノイズ差別化・将来拡張用)
};

// ==================================================================
// CbParticleStatic — エミッタ設定値 (変更時のみ upload, 176 bytes)
//
// ParticleEmitterDesc から組み立てる。
// デザイナーが設定した値を保持し、フレームをまたいで変わらない。
// ==================================================================
CB_PARTICLE_STRUCT(CbParticleStatic, SLOT_CB_PARTICLE_STATIC)
{
    // ---- 発生形状 ---- (offset 0-95)
    float4 emit_local_offset;       // offset   0: xyz=ローカルオフセット, w=Y方向揺らぎ幅

    float2 emit_radius;             // offset  16: 発生半径 min/max
    float2 emit_size;               // offset  24: サイズ x:誕生 y:消滅

    float2 cone_angle;              // offset  32: 放出角度範囲 (rad)
    float2 emit_speed;              // offset  40: 初速度範囲

    float2 emit_angular_speed;      // offset  48: 角速度範囲 (rad/s)
    float2 lifespan;                // offset  56: 寿命範囲 (秒)

    float2 spawn_delay;             // offset  64: 出現遅延範囲 (秒)
    float2 fade_duration;           // offset  72: フェード x:in y:out (秒)

    float4 manual_color;            // offset  80: 手動カラー (RGBA 乗算)

    // ---- 物理 / ノイズ ---- (offset 96-127)
    float  noise_scale;             // offset  96
    float  noise_strength;          // offset 100
    float  gravity;                 // offset 104: 重力 (マイナス=下方向)
    float  velocity_stretch;        // offset 108: 速度ストレッチ係数

    float2 uv_scroll_speed;         // offset 112: UV スクロール速度
    float2 emit_stretch;            // offset 120: 発生形状 x:X軸倍率 y:Z軸倍率

    // ---- 見た目 ---- (offset 128-143)
    float2 particle_scale;          // offset 128: 誕生/消滅サイズ倍率 (spawn 時にベイク)
    uint2  sprite_sheet_grid;       // offset 136: スプライトシート分割数 (列, 行)

    // ---- モード / 設定 ---- (offset 144-175)
    int    sprite_anim_mode;        // offset 144: 0:ランダム 1:寿命連動
    int    render_mode;             // offset 148: 0:加算 1:半透明
    int    color_mode;              // offset 152: 0:手動 1:カラーランプ
    int    particle_type;           // offset 156: 0:Point 1:Quad

    int    max_count;               // offset 160: 粒子の最大数
    int    emit_mode;               // offset 164: 0:継続 1:バースト
    int    burst_count;             // offset 168: バースト時の発生数
    float  _pad_static;             // offset 172

    // ---- Force Field ---- (offset 176-207)
    float4 attractor_pos;           // offset 176: xyz=中心, w=強さ(正:引力 負:斥力)
    float  attractor_radius;        // offset 192: 影響半径 (0=無限)
    float  vortex_strength;         // offset 196: 中心軸(Y)まわりの渦
    float  drag;                    // offset 200: 速度減衰 (1/s)
    uint   force_flags;             // offset 204: bit0=引力/渦 有効, bit1=local空間

    // ---- 発生形状: 線分(剣の刃に沿って撒く) ---- (offset 208-223)
    float3 emit_line_end;           // offset 208: 線分の終点。emit_local_offset を始点とするローカルオフセット(刃先)
    float  emit_shape;              // offset 220: 0:既存の楕円ディスク 1:線分(base→tip をemit_rotationで刃に沿わせる)
};
// 合計: 224 bytes

// ==================================================================
// CbParticleDynamic — ランタイム値 (毎フレーム upload, 48 bytes)
//
// Transform + 時間 + バーストトリガーのみを保持する。
// 将来的に多数のエミッタを扱う場合、この CB だけを更新すれば済む。
// ==================================================================
CB_PARTICLE_STRUCT(CbParticleDynamic, SLOT_CB_PARTICLE_DYNAMIC)
{
    float4 emit_rotation;           // offset  0: エミッタ回転 (クォータニオン)

    float3 world_position;          // offset 16: エミッタのワールド座標 (Transform 由来)
    float  time;                    // offset 28: 累積経過時間 (秒)

    float  delta_time;              // offset 32: 1フレームの秒数
    float  burst_trigger;           // offset 36: >0 でバースト発動、システムが消費して 0 に戻す
    float  emit_enabled;            // offset 40: 継続放出モードの発生ゲート (0:停止 それ以外:発生)。アニメ連動用。
    float  _pad_dynamic;            // offset 44

    // ---- 前フレームのエミッタ姿勢（サブフレーム補間でスポーンを軌道上に連続配置する） ----
    float4 prev_emit_rotation;      // offset 48: 前フレームのワールド回転
    float3 prev_world_position;     // offset 64: 前フレームのワールド座標
    float  _pad_dynamic2;           // offset 76
};
// 合計: 80 bytes

// ==================================================================
// C++ 側のみ: レイアウト検証
// ==================================================================
#ifdef __cplusplus
static_assert(sizeof(GpuParticle) == 80, "GpuParticle size mismatch");
static_assert(sizeof(CbParticleStatic) == 224, "CbParticleStatic size mismatch");
static_assert(sizeof(CbParticleDynamic) == 80, "CbParticleDynamic size mismatch");

} // namespace GpuData

#undef CB_PARTICLE_STRUCT
#else
#undef CB_PARTICLE_STRUCT
#endif

#endif // PARTICLE_SHARED_H