// Graphics/Shader/ShaderShared.h
//
// 【C++ と HLSL で共有する構造体レイアウトの単一ソース】
// スコープ : エンジン全体で使われる構造体のみを定義する。
//    特定システム固有の構造体は各システムの SharedHeader に分ける。
//    例: パーティクル → Particle/ParticleShared.h
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
//   HLSL フィールド名をそのまま使う（C++側が合わせる）。
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

    // 時間パラメータ（Graphics::Update が毎フレーム書き込む）
    //   x: 経過秒(time)  y: デルタ秒(deltaTime)  z: sin(経過秒)  w: 予約
    //   末尾に追加しているため既存フィールドのオフセットは不変。
    //   Common.hlsli を include する全マテリアルから gTimeParams で参照可能。
    float4 gTimeParams;

    float4 gScreenParams;
};

// ==================================================================
// カスケードシャドウマップ定数バッファ
//   HLSL フィールド名をそのまま使う（snake_case は HLSL 起源）
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
//   HLSL フィールド名をそのまま使う（g プレフィックスは HLSL 起源）
// ==================================================================
CB_STRUCT(CbFog, SLOT_CB_FOG)
{
    float4 gFogColor;
    float4 gFogParams;      // x:start y:end z:heightStart w:heightDensity
    float4 gFogCenter;
    float4 gFogNoiseParams; // x:scale y:strength z:speedX w:speedY
    float4 gFogRimColor;
    float4 gFogRimParams;   // x:power y:strength
    float4 gSkyFogParams;   // x:horizon(フォグ開始の視線高 dir.y) y:softness(フェード幅)
};

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
static_assert(sizeof(CbScene) == 272, "CbScene size mismatch vs HLSL");
static_assert(sizeof(CbShadow) == 240, "CbShadow size mismatch vs HLSL");
static_assert(sizeof(CbFog) == 112, "CbFog size mismatch vs HLSL");

} // namespace GpuData

#undef ROW_MAJOR
#undef CB_STRUCT
#else
#undef ROW_MAJOR
#undef CB_STRUCT
#endif

#endif // SHADER_SHARED_H