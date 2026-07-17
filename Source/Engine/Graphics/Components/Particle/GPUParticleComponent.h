#pragma once
#include "Engine/Graphics/Shader/Shared/ParticleShared.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/ECS/Common/CCL_Types.h" // EntityID (アニメ連動オーナーのキャッシュ用)
#include <cstring>
#include <d3d11.h>
#include <wrl/client.h>   // ComPtr (proceduralRampSRV 用)

// =========================================================================
// ParticleEmitterDesc — 権威データ（デザイナーが編集・シリアライズ対象）
//
// 設計原則:
//   - C++ のみ。HLSL レイアウト（CbParticleStatic）に一切依存しない。
//   - デフォルト値付き → ゼロ初期化で「動くエミッタ」になる。
//   - シリアライズはこれだけ見ればよい。
//   - POD ライク維持 (std::string / ComPtr を持たない)。
//
// 命名: camelCase。CbParticleStatic の snake_case フィールドと対応関係が
//       BuildStaticCb() で一目でわかるよう、語幹は揃えている。
// =========================================================================
struct ParticleEmitterDesc
{
    // ---- 発生形状 ----
    GpuData::float2 emitRadius        = { 0.0f, 0.5f };    // emit_radius    発生半径 min/max
    GpuData::float2 emitSize          = { 0.1f, 0.05f };   // emit_size      サイズ 誕生/消滅
    GpuData::float2 coneAngle         = { 0.0f, 0.3f };    // cone_angle     放出角度範囲 (rad)
    GpuData::float2 emitSpeed         = { 1.0f, 3.0f };    // emit_speed     初速度範囲
    GpuData::float2 angularSpeed      = { 0.0f, 0.0f };    // emit_angular_speed
    GpuData::float2 lifespan          = { 1.0f, 2.0f };    // lifespan       寿命範囲 (秒)
    GpuData::float2 spawnDelay        = { 0.0f, 0.0f };    // spawn_delay    出現遅延範囲 (秒)
    GpuData::float2 fadeDuration      = { 0.2f, 0.3f };    // fade_duration  フェード in/out (秒)

    // ---- 発生位置 ----
    GpuData::float3 localOffset       = { 0, 0, 0 };       // emit_local_offset.xyz (線分モードでは始点=根本)
    float           localOffsetY      = 0.0f;              // emit_local_offset.w (Y揺らぎ幅・ディスクモード用)
    GpuData::float2 emitStretch       = { 1.0f, 1.0f };    // emit_stretch   x:X軸 y:Z軸倍率

    // ---- 発生形状 (剣の刃に沿って撒く線分モード) ----
    int             emitShape         = 0;                 // emit_shape 0:ディスク(既存) 1:線分(剣)
    GpuData::float3 lineEnd           = { 0.0f, 1.0f, 0.0f }; // emit_line_end: localOffset を始点とする刃先までのローカルオフセット

    // ---- 色 ----
    GpuData::float4 color             = { 1, 1, 1, 1 };    // manual_color
    int             colorMode         = 0;                  // 0:手動 1:カラーランプ
    int             renderMode        = 0;                  // 0:加算 1:半透明

    // ---- 物理 ----
    float           gravity           = 0.0f;
    float           velocityStretch   = 0.0f;
    float           noiseScale        = 1.0f;
    float           noiseStrength     = 0.0f;
    GpuData::float2 uvScrollSpeed     = { 0.0f, 0.0f };

    // ---- 見た目 ----
    GpuData::float2 scale             = { 1.0f, 1.0f };    // particle_scale (spawn 時にベイク)
    GpuData::uint2  spriteGrid        = { 1, 1 };           // sprite_sheet_grid
    int             spriteMode        = 0;                  // sprite_anim_mode 0:ランダム 1:寿命

    // ---- 放出設定 ----
    int             maxCount          = 1000;              // max_count ★唯一の権威
    int             particleType      = 1;                  // particle_type 0:Point 1:Quad
    int             emitMode          = 0;                  // emit_mode 0:継続 1:バースト
    int             burstCount        = 0;                  // burst_count

    // ---- Force Field (引力/斥力/渦/ドラッグ。全て 0/既定 = 無効で従来挙動) ----
    GpuData::float3 attractorPos      = { 0, 0, 0 };   // 中心 (attractorLocal=true なら emitter 相対)
    float           attractorStrength = 0.0f;          // 正:引力 負:斥力 (中心での加速度 u/s^2)
    float           attractorRadius   = 0.0f;          // 影響半径 (0=無限, >0 で滑らかに減衰)
    float           vortexStrength    = 0.0f;          // 中心軸まわりの渦 (0=無し。引力と併用推奨)
    float           drag              = 0.0f;          // 速度減衰 1/s (0=無し)
    bool            attractorLocal    = true;          // true:emitter相対 false:world絶対

    // ---- アセット参照（POD 維持のため固定長配列） ----
    char            texturePath[128]  = {};
    char            colorRampPath[128]= {};

    ParticleEmitterDesc()
    {
        std::memset(texturePath,    0, sizeof(texturePath));
        std::memset(colorRampPath,  0, sizeof(colorRampPath));
    }

    /// @brief desc を変更した後に呼ぶ。次フレームで staticCB が再 upload される。
    //void MarkDescChanged() { _staticDirty = true; }   // ★ step1から修正: _rampDirty を分離
    /// @brief gradientStops を変更した後に呼ぶ。次フレームで手続きランプを再ベイク。
    //void MarkGradientChanged() { _rampDirty = true; }
};

// =========================================================================
// GPUParticleComponent — ランタイム状態のみを保持する薄いコンポーネント
//
// 設計原則:
//   - 権威データは desc に集約。CB は毎フレームシステムが desc から組み立てる。
//   - _staticDirty: desc が変更された時だけ staticCB を再 upload するフラグ。
//     TriggerBurst / isInitialized のような頻繁な変更は dynamicCB 側で処理。
// =========================================================================
struct GPUParticleComponent
{
    ParticleEmitterDesc  desc;              // 権威データ（編集・保存対象）

    // ---- ランタイム状態 ----
    TextureHandle        texture;
    TextureHandle        colorRamp;
    ParticleBufferHandle computeBuffer;

    bool  isInitialized = false;            // false → UpdateSystem がバッファを再構築
    bool  needsGpuInit  = true;            // true  → InitCS を走らせる
    bool  _staticDirty  = true;            // true  → staticCB を再 upload する
    float time          = 0.0f;            // 累積経過時間（フレームをまたいで保持）
    float burstTrigger  = 0.0f;            // TriggerBurst() で立て、UpdateSystem が消費

    // ---- アニメ連動の発生ゲート（TrailComponent と同じ区間ゲート方式） ----
    // emitting=false の間、UpdateSystem は emit_enabled=0 を GPU へ渡し、
    // 継続モードの粒子の「再生成」だけを止める（既存粒子は寿命まで生きて尾を引いて消える）。
    bool emitting = true;                   ///< 発生マスタースイッチ(runtime)。GPUへ emit_enabled として渡る。

    /// @brief true: 祖先(Animator)の Particle 区間イベントで emitting を自動駆動する。
    ///        false: emitting は手動（常時ON）。既定は手動。※これのみシリアライズ対象。
    bool animControlled = false;

    /// @brief animControlled 時に解決した「Animator を持つ祖先」。Invalid は未解決(非シリアライズ)。
    ///        ParticleAnimationSyncSystem が親を辿って一度だけ解決しキャッシュする。
    CCL::ECS::EntityID ownerEntity = CCL::ECS::InvalidEntityID;

    // ---- サブフレーム補間用: 前フレームのエミッタ姿勢(runtime・非シリアライズ) ----
    // 速い剣の振りでもスポーンが1フレームの「櫛」にならず軌道上に連続配置されるよう、
    // UpdateSystem が毎フレーム「前フレームのワールド姿勢」を保持し、spawn 時に補間する。
    GpuData::float3 prevWorldPos    = { 0.0f, 0.0f, 0.0f };
    GpuData::float4 prevWorldRot    = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool            hasPrevTransform = false; // false の初回は現在姿勢で初期化(スメアなし)

    // ---- コンストラクタ ----
    GPUParticleComponent()  = default;
    explicit GPUParticleComponent(int count) { desc.maxCount = count; }

    /// @brief バーストを1フレームだけ発動させる。
    void TriggerBurst() { burstTrigger = 1.0f; }

    /// @brief desc を変更した後に呼ぶ。次フレームで staticCB が再 upload される。
    void MarkDescChanged() { _staticDirty = true; }
};