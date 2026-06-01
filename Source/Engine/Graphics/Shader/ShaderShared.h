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
#ifdef __cplusplus
#include <DirectXMath.h>
#include <cstdint>

namespace GpuData {

    using float2 = DirectX::XMFLOAT2;
    using float3 = DirectX::XMFLOAT3;
    using float4 = DirectX::XMFLOAT4;
    using float4x4 = DirectX::XMFLOAT4X4;   // 行優先ストレージ = HLSL の row_major と一致
    using uint = uint32_t;

#define ROW_MAJOR                                 // HLSLのrow_major、C++では無し
#define CB_STRUCT(name, slot) struct name         // C++では普通のstruct
#else
#define ROW_MAJOR row_major
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
// C++ 側のみ: レイアウト検証
//   フィールドを足して padding を忘れたら、ここで即コンパイルエラーになる。
//   = 「動くのに色が壊れる」系バグがビルド時に止まる。
// ==================================================================
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


} // namespace GpuData

#undef ROW_MAJOR
#undef CB_STRUCT
#else
#undef ROW_MAJOR
#undef CB_STRUCT
#endif

#endif // SHADER_SHARED_H