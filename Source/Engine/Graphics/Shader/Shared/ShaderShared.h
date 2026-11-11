// ===========================================================================
// 概要: C++ と HLSL の両方から include し、定数バッファなどの構造体の並びを1か所で決めるヘッダー
//
// 責務:
//   - エンジン全体で使う構造体（ライト・インスタンス・シーン・影・フォグ）を定義する
//   - C++ では GpuData::CbLight などの struct、HLSL では cbuffer CbLight になるよう書き分ける
//   - C++ 側で大きさを static_assert し、HLSL との食い違いをビルド時に見つける
//
// 備考: 特定のシステムだけで使う構造体は別のヘッダーに分ける（パーティクルは ParticleShared.h、
//       ポストプロセスは PostProcessShared.h）
//       HLSL からも読むので #pragma once ではなくインクルードガードを使う
// ===========================================================================

#ifndef SHADER_SHARED_H
#define SHADER_SHARED_H

#include "Engine/Graphics/Shader/ShaderResources.h"

// ==================================================================
// 配列の大きさ（C++ と HLSL で違うと定数バッファの並びがずれるので、ここで共有する）
// ==================================================================
#define MAX_POINT_LIGHTS 8
#define MAX_SPOT_LIGHTS 8
#define MAX_BONES 256
#define SHADOW_CASCADE_COUNT 3 // カスケードシャドウマップの枚数

// ==================================================================
// C++ と HLSL の書き方の違いをそろえる
// ==================================================================

// __cplusplus は C++ のコンパイラだけが定義する。HLSL のコンパイラ（fxc）では定義されない
#ifdef __cplusplus
#include <DirectXMath.h>
#include <cstdint>

// C++ には float2 などがないので、同じ名前の別名を作る
// この namespace はファイルの最後で閉じる（HLSL では namespace を開かない）
namespace GpuData
{

    using float2   = DirectX::XMFLOAT2;
    using float3   = DirectX::XMFLOAT3;
    using float4   = DirectX::XMFLOAT4;
    using float4x4 = DirectX::XMFLOAT4X4; // 行優先ストレージ = HLSL の row_major と一致
    using uint     = uint32_t;
    using uint2    = DirectX::XMUINT2;

// C++ では何も付けない（XMFLOAT4X4 はもともと行優先で並ぶ）
#define ROW_MAJOR
// C++ では struct name { ... } になり、GpuData::name で使える
#define CB_STRUCT(name, slot) struct name

#else
// HLSL は float2 などを持っているのでそのまま使う
#define ROW_MAJOR row_major

// HLSL では cbuffer name : register(bN) になる
#define CB_STRUCT(name, slot) cbuffer name : register(REG_CB(slot))
#endif

    // ==================================================================
    // ライト
    //   どれも大きさを 16 バイトの倍数にしてある。そうすると HLSL の cbuffer の配列で
    //   要素の間に詰め物が入らず、C++ の配列と並びが一致する
    //   メンバを足すときも 16 バイトの倍数を崩さないこと
    // ==================================================================
    struct DirectionalLight
    {
        float3 direction;
        float  intensity;
        float3 color;
        float  padding;
        float4 gSkyColor;    // 半球ライティングの空の色。rgb = 色, a = 強さ
        float4 gGroundColor; // 地面の色。rgb = 色, a = 強さ
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
        float  active; // 0 なら使わない
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
    // インスタンス1個分（StructuredBuffer の1要素。C++ で送るのは InstanceBuffer.h の InstanceData）
    // ==================================================================
    struct InstanceData
    {
        ROW_MAJOR float4x4 World;
        float4             customParams; // 意味はシェーダーごと（Phong は z = 被弾フラッシュ）
    };

    // ==================================================================
    // 定数バッファ
    // ==================================================================
    CB_STRUCT(CbLight, SLOT_CB_LIGHT)
    {
        DirectionalLight gDirLight;
        PointLight       gPointLights[MAX_POINT_LIGHTS];
        SpotLight        gSpotLights[MAX_SPOT_LIGHTS];
        int              gPointLightCount;
        int              gSpotLightCount;
        float2           gPadding;
    };

    CB_STRUCT(CbSkeleton, SLOT_CB_SKELETON)
    {
        ROW_MAJOR float4x4 boneTransforms[MAX_BONES];
    };

    // ==================================================================
    // カメラ・シーン
    //   書くのは Graphics::Update だけ。他から UpdateSubresource しないこと
    //   メンバの名前は HLSL での名前をそのまま使う
    // ==================================================================
    CB_STRUCT(CbScene, SLOT_CB_SCENE)
    {
        ROW_MAJOR float4x4 viewProjection;
        float4             lightDirection;
        float4             lightColor;
        float4             cameraPosition;
        ROW_MAJOR float4x4 gView;
        ROW_MAJOR float4x4 gProjection;

        // x = 経過時間（秒）, y = 前のフレームからの時間（秒）, z = sin(経過時間), w = 使っていない
        float4 gTimeParams;

        // x = 幅（px）, y = 高さ（px）, z = 1 / 幅, w = 1 / 高さ
        float4 gScreenParams;
    };

    // ==================================================================
    // カスケードシャドウマップ（メンバの名前は HLSL での名前をそのまま使う）
    // ==================================================================
    CB_STRUCT(CbShadow, SLOT_CB_SHADOW)
    {
        ROW_MAJOR float4x4 light_view_projection[SHADOW_CASCADE_COUNT];
        float4             cascadeSplits;
        float4             shadow_color;
        float              shadow_attenuation;
        float              shadow_bias;
        int                currentCascadeIndex; // ShadowPass が今描いているカスケード
        float              shadow_padding;
    };

    // ==================================================================
    // フォグ（メンバの名前は HLSL での名前をそのまま使う）
    // ==================================================================
    CB_STRUCT(CbFog, SLOT_CB_FOG)
    {
        float4 gFogColor;
        float4 gFogParams;      // x = 始まる距離（m）, y = 終わる距離（m）, z = 高さフォグの基準の高さ（m）, w = 高さで薄くなる割合（1/m）
        float4 gFogCenter;    // 距離を測る中心（ワールド座標）
        float4 gSkyFogParams; // x = 空にフォグがかかり始める視線の高さ（向きの y）, y = ぼかす幅
    };

// ==================================================================
// C++ だけ: 大きさの確認
// ==================================================================
//   C++ と HLSL で詰め方の決まりが違うので、大きさを static_assert で確かめる
//   HLSL の cbuffer はメンバが 16 バイトの区切りをまたがないよう詰め物を入れるが、C++ は入れない
//   ずれると動きはするのに、以後の値が1つずつずれて読まれ、色などがおかしくなる
#ifdef __cplusplus
    static_assert(sizeof(DirectionalLight) == 64, "DirectionalLight size mismatch vs HLSL");
    static_assert(sizeof(PointLight) == 48, "PointLight size mismatch vs HLSL");
    static_assert(sizeof(SpotLight) == 80, "SpotLight size mismatch vs HLSL");
    static_assert(sizeof(InstanceData) == 80, "InstanceData size mismatch vs HLSL");
    static_assert(sizeof(CbLight) == 1104, "CbLight size mismatch vs HLSL");
    static_assert(sizeof(CbSkeleton) == 16384, "CbSkeleton size mismatch vs HLSL");
    static_assert(sizeof(CbScene) == 272, "CbScene size mismatch vs HLSL");
    static_assert(sizeof(CbShadow) == 240, "CbShadow size mismatch vs HLSL");
    static_assert(sizeof(CbFog) == 64, "CbFog size mismatch vs HLSL");

} // namespace GpuData

#undef ROW_MAJOR
#undef CB_STRUCT
#else
#undef ROW_MAJOR
#undef CB_STRUCT
#endif

#endif // SHADER_SHARED_H