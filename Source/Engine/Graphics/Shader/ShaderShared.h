// Graphics/Shader/ShaderShared.h
//
// 【C++ と HLSL で共有する構造体レイアウトの単一ソース】
// ShaderResources.h がスロット番号を共有するのと同じ仕組みを、
// 構造体のフィールド配置と配列サイズに広げたもの。
// これで「C++側とHLSL側でフィールドがズレて無言で壊れる」事故を構造的に防ぐ。
//
// 使い方:
//   C++  : #include ".../ShaderShared.h"  → Shader::CbLight などが使える
//   HLSL : #include ".../ShaderShared.h"  → cbuffer CbLight が定義される

#ifndef SHADER_SHARED_H
#define SHADER_SHARED_H

#include "Engine/Graphics/Shader/ShaderResources.h"  // SLOT_*, REG_CB

// ==================================================================
// 配列サイズ（C++とHLSLで必ず一致させたい「魔法の数字」をここに集約）
// ==================================================================
#define MAX_POINT_LIGHTS 8
#define MAX_SPOT_LIGHTS  8
#define MAX_BONES        256
#define SHADOW_CASCADE_COUNT 3   // カスケードシャドウの層数

// ==================================================================
// 型エイリアスと修飾子（C++ / HLSL の差を吸収する）
// ==================================================================

// 今コンパイルをしているのがC++側かHLSL側かを判定するマクロ
// trueならC++、falseならHLSLとみなす
#ifdef __cplusplus
#include <DirectXMath.h>
#include <cstdint>

// C++の場合はfloat2などないのでusingで定義する
namespace GpuData {

    using float2 = DirectX::XMFLOAT2;
    using float3 = DirectX::XMFLOAT3;
    using float4 = DirectX::XMFLOAT4;
    using float4x4 = DirectX::XMFLOAT4X4;   // 行優先ストレージ = HLSL の row_major と一致
    using uint = uint32_t;
	using uint2 = DirectX::XMUINT2;

#define ROW_MAJOR                                 // HLSLのrow_major、C++では無し
    // C++では普通のstructになる
	// struct "name" { ... } と定義されるので、Shader::name でアクセスできる
#define CB_STRUCT(name, slot) struct name         

	// HLSLの場合はfloat2などの型があるのでそのまま使う
#else
#define ROW_MAJOR row_major

// HLSLでは cbuffer "name" : register(bN) と定義される
#define CB_STRUCT(name, slot) cbuffer name : register(REG_CB(slot))
#endif

// ==================================================================
// ライト構造体（leaf）
//   ※ 各構造体は 16 バイトの倍数になるよう padding 済み。
//      これで cbuffer 配列の要素間パディングが入らず C++ 配列と一致する。
//      フィールドを足すときは 16 バイト境界を崩さないこと。
// ==================================================================
struct DirectionalLight
{
    float3 direction;
    float  intensity;
    float3 color;
    float  padding;
    float4 gSkyColor;     // rgb:色 a:強度（半球ライティング）
    float4 gGroundColor;  // rgb:色 a:強度
};

struct PointLight
{
    float3 position;
    float  range;
    float3 color;
    float  intensity;
    float  constantAttenuation;
    float  linearAttenuation;
    float  quadraticAttenuation;
    float  active;
};

struct SpotLight
{
    float3 position;
    float  range;
    float3 direction;
    float  innerCos;
    float3 color;
    float  outerCos;
    float  intensity;
    float  active;
    float  padding1;
    float  padding2;
    float  constantAttenuation;
    float  linearAttenuation;
    float  quadraticAttenuation;
    float  padding;
};

// ==================================================================
// インスタンスデータ（StructuredBuffer の1要素）
// ==================================================================
struct InstanceData
{
    ROW_MAJOR float4x4 World;
    float4 customParams; // x=ディゾルブ進行度など
};

// ==================================================================
// 定数バッファ
// ==================================================================
CB_STRUCT(CbLight, SLOT_CB_LIGHT)
{
    DirectionalLight gDirLight;
    PointLight       gPointLights[MAX_POINT_LIGHTS];
    SpotLight        gSpotLights[MAX_SPOT_LIGHTS];
    int    gPointLightCount;
    int    gSpotLightCount;
    float2 gPadding;
};

CB_STRUCT(CbSkeleton, SLOT_CB_SKELETON)
{
    ROW_MAJOR float4x4 boneTransforms[MAX_BONES];
};


// ==================================================================
// カメラ / シーン定数バッファ
//   ★ HLSL フィールド名をそのまま使う（C++側が合わせる）。
//      Graphics::Update() のみが書き込む。他システムは UpdateSubresource 禁止。
// ==================================================================
CB_STRUCT(CbScene, SLOT_CB_SCENE)
{
    ROW_MAJOR float4x4 viewProjection;
    float4 lightDirection;
    float4 lightColor;
    float4 cameraPosition;
    ROW_MAJOR float4x4 gView;        // HLSL名: gView
    ROW_MAJOR float4x4 gProjection;  // HLSL名: gProjection
};

// ==================================================================
// カスケードシャドウマップ定数バッファ
//   ★ HLSL フィールド名をそのまま使う（snake_case は HLSL 起源）
// ==================================================================
CB_STRUCT(CbShadow, SLOT_CB_SHADOW)
{
    ROW_MAJOR float4x4 light_view_projection[SHADOW_CASCADE_COUNT];
    float4 cascadeSplits;
    float4 shadow_color;
    float  shadow_attenuation;
    float  shadow_bias;
    int    currentCascadeIndex;
    float  shadow_padding;
};

// ==================================================================
// フォグ定数バッファ
//   ★ HLSL フィールド名をそのまま使う（g プレフィックスは HLSL 起源）
// ==================================================================
CB_STRUCT(CbFog, SLOT_CB_FOG)
{
    float4 gFogColor;
    float4 gFogParams;      // x:start y:end z:heightStart w:heightDensity
    float4 gFogCenter;
    float4 gFogNoiseParams; // x:scale y:strength z:speedX w:speedY
    float4 gFogRimColor;
    float4 gFogRimParams;   // x:power y:strength
};


// ==================================================================
// GPUパーティクルデータ（StructuredBuffer の1要素 / 粒子1個の状態）
//
// ★ HLSL 側の struct particle と完全一致させること。
//    フィールドを追加する場合は particle.hlsli も同時に更新し、
//    下の static_assert を必ず更新すること。
// ==================================================================
struct GpuParticle
{
    int    state;           // 0:死亡/待機  1:生存中
    float  angle;           // 現在の回転角度 (rad)
    int    pad0[2];
    float4 color;           // 現在のカラー (RGBA)
    float3 position;        // ワールド座標
    float  mass;            // 質量（重力・ノイズの受けやすさ）
    float  angular_speed;   // 角速度 (rad/s)
    float3 velocity;        // 移動速度ベクトル
    float  lifespan;        // 最大寿命 (秒)
    float  age;             // 経過時間 (秒、負=発生前待機)
    float2 size;            // x:出現サイズ, y:消滅サイズ
    uint   chip;            // スプライトシートのコマ番号
    uint   type;            // 0:Point 1:Quad
    float2 scale;           // 全体スケール乗数
};

// ==================================================================
// パーティクル定数バッファ（エミッタ全体の設定）
//
// フィールドは「権威データ (Desc 由来)」と「ランタイム値」に分類される。
// C++ 側は ParticleEmitterDesc + ランタイム値から毎フレーム組み立てる。
// HLSL 側は particle.hlsli に同名フィールドを持つ cbuffer として宣言される。
//
// 【レイアウト変更履歴】
//   - emission_position  → emission_local_offset (xyz: ローカルオフセット, w: offsetY)
//   - particle_offset_y  → emission_local_offset.w に統合（フィールド削減）
//   - emission_stretch_x/z → float2 emission_stretch に統合（last row 整理）
//   - 最終16バイト: emission_stretch(8) + burst_trigger(4) + _pad_end(4)
//
// 【将来: 静的/動的 CB 分割（TODO）】
//   エミッタ数が多くなった場合のパフォーマンス最適化として、
//   以下の2つに分割すること。実装時は ShaderResources.h に
//   SLOT_CB_PARTICLE_DYN を追加し、GpuParticleBufferSet に
//   dynamicCB を追加する。
//
//   CbParticleStatic  (b13) — Desc 由来。変更時のみ upload。
//   CbParticleDynamic (b14) — world_position / time / delta_time /
//                              burst_trigger / emission_rotation。
//                              毎フレーム upload (32 bytes 程度)。
// ==================================================================
CB_STRUCT(CbParticle, SLOT_CB_PARTICLE)
{
    // ---- Desc 由来（変更時のみ更新） ----

    // 発生位置ローカルオフセット (xyz) + Y方向揺らぎ幅 (w)
    //   world_position (ランタイム) に加算して粒子初期位置を決める
    float4 emission_local_offset;       // offset   0

    // ★ ランタイム ---- エミッタのワールド回転 (クォータニオン)
    float4 emission_rotation;           // offset  16

    float2 emission_offset;             // offset  32  発生半径 min/max
    float2 emission_size;               // offset  40  サイズ x:出現 y:消滅
    float2 emission_cone_angle;         // offset  48  放出角度範囲 (rad)
    float2 emission_speed;              // offset  56  初速度範囲
    float2 emission_angular_speed;      // offset  64  角速度範囲
    float2 lifespan;                    // offset  72  寿命範囲 (秒)
    float2 spawn_delay;                 // offset  80  出現遅延範囲 (秒)
    float2 fade_duration;               // offset  88  フェード in/out (秒)
    float4 manual_color;                // offset  96  手動カラー (RGBA 乗算)

    // ★ ランタイム ----
    float  time;                        // offset 112  累積経過時間 (秒)
    float  delta_time;                  // offset 116  1フレームの秒数
    float  noise_scale;                 // offset 120  乱流ノイズスケール
    float  noise_strength;              // offset 124  乱流ノイズ強度

    float  gravity;                     // offset 128  重力 (マイナス=下方向)
    float  velocity_stretch;            // offset 132  速度ストレッチ係数
    float2 uv_scroll_speed;             // offset 136  UVスクロール速度

    int    sprite_anim_mode;            // offset 144  0:ランダム 1:寿命連動
    int    render_mode;                 // offset 148  0:加算 1:半透明
    float2 padding1;                    // offset 152

    uint2  sprite_sheet_grid;           // offset 160  スプライトシート分割数 (列, 行)
    float2 particle_scale;              // offset 168  全体スケール乗数

    // ★ ランタイム ----
    float3 world_position;              // offset 176  エミッタのワールド座標 (Transformから)
    int    color_mode;                  // offset 188  0:手動 1:テンパーチャート

    int    max_particle_count;          // offset 192  粒子の最大数
    int    type;                        // offset 196  0:Point 1:Quad
    int    emission_mode;               // offset 200  0:継続 1:バースト
    int    burst_count;                 // offset 204  バースト時の発生数

    // 最終行 (offset 208-223) ── 整列ルール: float2(8B) は 8B 境界から開始
    float2 emission_stretch;            // offset 208  XZ 方向ストレッチ (x:X軸, y:Z軸)
    // ★ ランタイム ----
    float  burst_trigger;               // offset 216  >0 でバースト発動
    float  _pad_end;                    // offset 220  padding
};
// 合計: 224 bytes


// ==================================================================
// C++ 側のみ: レイアウト検証
//   フィールドを足して padding を忘れたら、ここで即コンパイルエラーになる。
//   = 「動くのに色が壊れる」系バグがビルド時に止まる。
// ==================================================================

// static_assert は C++11 以降で使えるコンパイル時アサーション。
// 条件が false ならコンパイルエラーになる。
// sizeof(StructType) はその構造体のサイズをバイト単位で返す演算子。

// なぜあるのか：
// 一つしかないのになぜassertを定義するのかというと
// C++とHLSL側でのパッキングのルールが違うから
// 例えばCbLight側でfloatを１個追加したら
// C++ではsizeof(CbLight)が1108になるけど、HLSL側ではパディングが入って1120になる
// このHLSLのパディングは、定数バッファの要素が16バイト境界に揃うようにするためのもので、
// C++側では自動的に入らないため、サイズがずれてしまう
#ifdef __cplusplus
static_assert(sizeof(DirectionalLight) == 64, "DirectionalLight size mismatch vs HLSL");
static_assert(sizeof(PointLight) == 48, "PointLight size mismatch vs HLSL");
static_assert(sizeof(SpotLight) == 80, "SpotLight size mismatch vs HLSL");
static_assert(sizeof(InstanceData) == 80, "InstanceData size mismatch vs HLSL");
static_assert(sizeof(CbLight) == 1104, "CbLight size mismatch vs HLSL");
static_assert(sizeof(CbSkeleton) == 16384, "CbSkeleton size mismatch vs HLSL");
static_assert(sizeof(CbScene) == 240, "CbScene size mismatch vs HLSL");
static_assert(sizeof(CbShadow) == 240, "CbShadow size mismatch vs HLSL");
static_assert(sizeof(CbFog) == 96, "CbFog size mismatch vs HLSL");
static_assert(sizeof(GpuParticle) == 96, "GpuParticle size mismatch vs HLSL");
static_assert(sizeof(CbParticle) == 224, "CbParticle size mismatch vs HLSL");


} // namespace GpuData

#undef ROW_MAJOR
#undef CB_STRUCT
#else
#undef ROW_MAJOR
#undef CB_STRUCT
#endif

#endif // SHADER_SHARED_H