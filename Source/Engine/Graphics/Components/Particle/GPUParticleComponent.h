#pragma once
#include "Engine/Graphics/Shader/ShaderShared.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include <cstring>
#include <d3d11.h>

// =========================================================================
// ParticleEmitterDesc — 権威データ（デザイナーが編集・シリアライズ対象）
//
// 設計原則:
//   - C++ のみ。HLSL レイアウト（CbParticle）に一切依存しない。
//   - デフォルト値付きなので「ゼロ初期化 = 動くエミッタ」になる。
//     ※ CbParticle は HLSL 共有構造体のためメンバ初期化子を持てない。
//        デフォルト値はここに集約することで解決。
//   - シリアライズ対象はこれだけ見ればよい。
//   - std::string / ComPtr を持たない（POD ライク維持）。
// =========================================================================
struct ParticleEmitterDesc {

    // ---- 発生形状 ----
    GpuData::float2 emissionOffset = { 0.0f, 0.5f };    // 発生半径 min/max
    GpuData::float2 emissionSize = { 0.1f, 0.05f };   // サイズ x:出現 y:消滅
    GpuData::float2 coneAngle = { 0.0f, 0.3f };    // 放出角度範囲 (rad)
    GpuData::float2 emissionSpeed = { 1.0f, 3.0f };    // 初速度範囲
    GpuData::float2 emissionAngularSpeed = { 0.0f, 0.0f };    // 角速度範囲
    GpuData::float2 lifespan = { 1.0f, 2.0f };    // 寿命範囲 (秒)
    GpuData::float2 spawnDelay = { 0.0f, 0.0f };    // 出現遅延範囲 (秒)
    GpuData::float2 fadeDuration = { 0.2f, 0.3f };    // フェード x:in y:out (秒)

    // ---- 発生位置 ----
    // world_position (TransformComponent 由来) からのローカルオフセット
    GpuData::float3 localOffset = { 0, 0, 0 };       // XYZ オフセット
    float           offsetY = 0.0f;              // Y 方向の発生揺らぎ幅

    // XZ 方向の形状ストレッチ（楕円エミッタに使用）
    GpuData::float2 emissionStretch = { 1.0f, 1.0f };    // x:X軸倍率, y:Z軸倍率

    // ---- 色 ----
    GpuData::float4 manualColor = { 1, 1, 1, 1 };
    int             colorMode = 0;                  // 0:手動 1:テンパーチャート
    int             renderMode = 0;                  // 0:加算 1:半透明

    // ---- 物理 ----
    float           gravity = 0.0f;
    float           velocityStretch = 0.0f;
    float           noiseScale = 1.0f;
    float           noiseStrength = 0.0f;
    GpuData::float2 uvScrollSpeed = { 0.0f, 0.0f };

    // ---- スケール / スプライト ----
    GpuData::float2 particleScale = { 1.0f, 1.0f };
    GpuData::uint2  spriteSheetGrid = { 1, 1 };
    int             spriteAnimMode = 0;                  // 0:ランダム 1:寿命連動

    // ---- 放出設定 ----
    int             maxParticleCount = 1000;              // ★ 唯一の権威 (maxParticles 廃止)
    int             type = 1;                  // 0:Point 1:Quad
    int             emissionMode = 0;                  // 0:継続 1:バースト
    int             burstCount = 0;

    // ---- アセット参照（シリアライズ用・POD 維持のため固定長配列） ----
    char            texturePath[128] = {};
    char            colorRampPath[128] = {};

    ParticleEmitterDesc() {
        std::memset(texturePath, 0, sizeof(texturePath));
        std::memset(colorRampPath, 0, sizeof(colorRampPath));
    }
};

// =========================================================================
// GPUParticleComponent — ランタイム状態のみ保持する薄いコンポーネント
//
// 設計原則:
//   - 権威データは desc に集約。CbParticle の直接保持を廃止。
//   - CbParticle はシステムが毎フレーム desc + ランタイム値から組み立てる。
//   - ランタイム状態（time / burstTrigger）のみここで持つ。
// =========================================================================
struct GPUParticleComponent {

    ParticleEmitterDesc desc;          // 権威データ（編集・シリアライズ対象）

    // ---- ランタイム状態 ----
    TextureHandle        texture;
    TextureHandle        colorRamp;
    ParticleBufferHandle computeBuffer;

    bool  isInitialized = false;       // false → UpdateSystem がバッファを再構築する
    bool  needsGpuInit = true;        // true  → InitCS を走らせる
    float time = 0.0f;        // 累積時間（フレームをまたいで保持が必要）
    float burstTrigger = 0.0f;        // TriggerBurst() で立て、UpdateSystem が消費して 0 に戻す

    // ---- コンストラクタ ----
    GPUParticleComponent() = default;

    explicit GPUParticleComponent(int count) {
        desc.maxParticleCount = count;
    }

    /// @brief バーストを1フレームだけ発動させる。
    ///        UpdateSystem が dispatch 後に burstTrigger を 0.0f に戻す。
    void TriggerBurst() { burstTrigger = 1.0f; }
};