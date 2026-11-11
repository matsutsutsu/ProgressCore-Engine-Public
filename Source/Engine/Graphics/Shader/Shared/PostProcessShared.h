// ===========================================================================
// 概要: ポストプロセスの各エフェクトで C++ と HLSL が共有する定数バッファ
//
// 責務:
//   - エフェクトごとの定数バッファの構造体を定義する
//   - C++ 側で大きさを static_assert する（理由は ShaderShared.h を参照）
//
// 備考: ShaderShared.h と同じ書き分けで、C++ では GpuData::CbBloomData など、HLSL では cbuffer になる
//       HLSL からも読むので #pragma once ではなくインクルードガードを使う
// ===========================================================================
#ifndef POST_PROCESS_SHARED_H
#define POST_PROCESS_SHARED_H

#include "Engine/Graphics/Shader/ShaderResources.h"

#ifdef __cplusplus
#include <DirectXMath.h>
namespace GpuData
{
    using float2 = DirectX::XMFLOAT2;
    using float3 = DirectX::XMFLOAT3;
    using float4 = DirectX::XMFLOAT4;
#define PP_STRUCT(name, slot) struct name
#else
#define PP_STRUCT(name, slot) cbuffer name : register(REG_CB(slot))
#endif

    // Bloom（32 バイト）
    PP_STRUCT(CbBloomData, SLOT_CB_BLOOM)
    {
        float threshold;
        float softKnee;
        float intensity;
        float radius;

        float texelWidth;
        float texelHeight;
        float pad[2];
    };

    // ToneMap（32 バイト）
    PP_STRUCT(CbToneMapData, SLOT_CB_TONEMAP)
    {
        float  exposure;
        float  chromaticAberration;
        float  pad2[2];
        float4 colorTint; // rgb = 掛ける色, w = 1
    };

    // RadialBlur（16 バイト）
    PP_STRUCT(CbRadialBlurData, SLOT_CB_RADIALBLUR)
    {
        float2 blurCenter;
        float  blurStrength;
        float  blurFalloff;
    };

    // Vignette（48 バイト）
    PP_STRUCT(CbVignetteData, SLOT_CB_VIGNETTE)
    {
        float4 vigColor;
        float  vigIntensity;
        float  vigRadius;
        float  vigSoftness;
        float  vigPulseSpeed;
        float  vigTime;
        float  vigPad[3];
    };

    // PauseBlur（16 バイト）
    PP_STRUCT(CbPauseBlurData, SLOT_CB_PAUSEBLUR)
    {
        float pauseBlurRadius; // サンプルする円の半径（UV）。0 ならぼかさない
        float pauseBlurPad[3];
    };

    // ChromaticAberration（16 バイト）
    PP_STRUCT(CbChromaticData, SLOT_CB_CHROMATIC)
    {
        float2 chromaCenter;
        float  chromaStrength;
        float  chromaFalloff;
    };

    // ColorGrading（32 バイト）
    PP_STRUCT(CbColorGradingData, SLOT_CB_COLORGRADING)
    {
        float4 gradeTint;
        float  gradeSaturation;
        float  gradeContrast;
        float  gradeBrightness;
        float  gradeMixAmount;
    };

    // DepthOfField（16 バイト）
    PP_STRUCT(CbDepthOfFieldData, SLOT_CB_DOF)
    {
        float dofFocusDistance;
        float dofFocusRange;
        float dofFalloff;
        float dofMaxBlur;
    };

    // SSAO（16 バイト）
    PP_STRUCT(CbSSAOData, SLOT_CB_SSAO)
    {
        float ssaoRadius;
        float ssaoIntensity;
        float ssaoBias;
        float ssaoMaxDistance;
    };

    // SSAOBlur（16 バイト）
    PP_STRUCT(CbSSAOBlurData, SLOT_CB_SSAO_BLUR)
    {
        float2 blurTexelStep;
        float  blurDepthSigma;
        float  blurMaxDistance;
    };

#ifdef __cplusplus
} // namespace GpuData
static_assert(sizeof(GpuData::CbBloomData) == 32, "CbBloomData size mismatch");
static_assert(
    sizeof(GpuData::CbToneMapData) == 32, "CbToneMapData size mismatch");
static_assert(
    sizeof(GpuData::CbRadialBlurData) == 16, "CbRadialBlurData size mismatch");
static_assert(
    sizeof(GpuData::CbVignetteData) == 48, "CbVignetteData size mismatch");
static_assert(
    sizeof(GpuData::CbChromaticData) == 16, "CbChromaticData size mismatch");
static_assert(
    sizeof(GpuData::CbPauseBlurData) == 16, "CbPauseBlurData size mismatch");
static_assert(
    sizeof(GpuData::CbColorGradingData) == 32, "CbColorGradingData size mismatch");
static_assert(
    sizeof(GpuData::CbDepthOfFieldData) == 16, "CbDepthOfFieldData size mismatch");
static_assert(
    sizeof(GpuData::CbSSAOData) == 16, "CbSSAOData size mismatch");
static_assert(
    sizeof(GpuData::CbSSAOBlurData) == 16, "CbSSAOBlurData size mismatch");
#endif

#endif // POST_PROCESS_SHARED_H