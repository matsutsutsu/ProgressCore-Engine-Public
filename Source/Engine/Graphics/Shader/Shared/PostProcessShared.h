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

    // ============================================================================
    // Bloom パラメータレイアウト（16バイトアライメント厳守：合計32バイト）
    // ============================================================================
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

    // ============================================================================
    // ToneMap パラメータレイアウト（合計32バイト）
    // ============================================================================
    PP_STRUCT(CbToneMapData, SLOT_CB_TONEMAP)
    {
        float  exposure;
        float  chromaticAberration;
        float  pad2[2];
        float4 colorTint; // RGB=Tint, W=1.0
    };

    // ============================================================================
    // Distortion パラメータレイアウト（合計16バイト）
    // ============================================================================
    PP_STRUCT(CbDistortionData, SLOT_CB_DISTORTION)
    {
        float time;
        float strength;
        float pad3[2];
    };

    // ============================================================================
    // RadialBlur パラメータレイアウト（合計16バイト）
    // ============================================================================
    PP_STRUCT(CbRadialBlurData, SLOT_CB_RADIALBLUR)
    {
        float2 blurCenter;
        float  blurStrength;
        float  blurFalloff;
    };

    // ============================================================================
    // Vignette パラメータレイアウト（合計48バイト）
    // ============================================================================
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

    // ============================================================================
    // ChromaticAberration パラメータレイアウト（合計16バイト）
    // ============================================================================
    // ============================================================================
    // PauseBlur パラメータレイアウト（合計16バイト）
    // ============================================================================
    PP_STRUCT(CbPauseBlurData, SLOT_CB_PAUSEBLUR)
    {
        float pauseBlurRadius; // サンプル円の半径(UV単位)。0 で素通し。
        float pauseBlurPad[3];
    };

    PP_STRUCT(CbChromaticData, SLOT_CB_CHROMATIC)
    {
        float2 chromaCenter;
        float  chromaStrength;
        float  chromaFalloff;
    };

    // ============================================================================
    // ColorGrading パラメータレイアウト（合計32バイト）
    // ============================================================================
    PP_STRUCT(CbColorGradingData, SLOT_CB_COLORGRADING)
    {
        float4 gradeTint;
        float  gradeSaturation;
        float  gradeContrast;
        float  gradeBrightness;
        float  gradeMixAmount;
    };

    // ============================================================================
    // DepthOfField パラメータレイアウト（合計16バイト）
    // ============================================================================
    PP_STRUCT(CbDepthOfFieldData, SLOT_CB_DOF)
    {
        float dofFocusDistance;
        float dofFocusRange;
        float dofFalloff;
        float dofMaxBlur;
    };

    // ============================================================================
    // SSAO パラメータレイアウト（合計16バイト）
    // ============================================================================
    PP_STRUCT(CbSSAOData, SLOT_CB_SSAO)
    {
        float ssaoRadius;
        float ssaoIntensity;
        float ssaoBias;
        float ssaoMaxDistance;
    };

    // ============================================================================
    // SSAOBlur パラメータレイアウト（合計16バイト）
    // ============================================================================
    PP_STRUCT(CbSSAOBlurData, SLOT_CB_SSAO_BLUR)
    {
        float2 blurTexelStep;
        float  blurDepthSigma;
        float  blurMaxDistance;
    };

    // ============================================================================
    // ContactShadow パラメータレイアウト（合計32バイト）
    // ============================================================================
    PP_STRUCT(CbContactShadowData, SLOT_CB_CONTACTSHADOW)
    {
        float  csRayLength;
        float  csThickness;
        float  csBias;
        float  csIntensity;
        float  csMaxDistance;
        float3 csPad;
    };

#ifdef __cplusplus
} // namespace GpuData
// C++側のサイズ検証。ずれていたらコンパイルが止まる
static_assert(sizeof(GpuData::CbBloomData) == 32, "CbBloomData size mismatch");
static_assert(
    sizeof(GpuData::CbToneMapData) == 32, "CbToneMapData size mismatch");
static_assert(
    sizeof(GpuData::CbDistortionData) == 16, "CbDistortionData size mismatch");
static_assert(
    sizeof(GpuData::CbRadialBlurData) == 16, "CbRadialBlurData size mismatch");
static_assert(
    sizeof(GpuData::CbVignetteData) == 48, "CbVignetteData size mismatch");
static_assert(
    sizeof(GpuData::CbChromaticData) == 16, "CbChromaticData size mismatch");
static_assert(
    sizeof(GpuData::CbColorGradingData) == 32, "CbColorGradingData size mismatch");
static_assert(
    sizeof(GpuData::CbDepthOfFieldData) == 16, "CbDepthOfFieldData size mismatch");
static_assert(
    sizeof(GpuData::CbSSAOData) == 16, "CbSSAOData size mismatch");
static_assert(
    sizeof(GpuData::CbSSAOBlurData) == 16, "CbSSAOBlurData size mismatch");
static_assert(
    sizeof(GpuData::CbContactShadowData) == 32, "CbContactShadowData size mismatch");
#endif

#endif // POST_PROCESS_SHARED_H