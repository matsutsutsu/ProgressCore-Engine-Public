#pragma once
// ===========================================================================
// JustEvadeEffectSystem.h
// -----------------------
// 役割:
//   JustEvadeEffectComponent(singleton) の timer を実時間で減衰させ、その強さを
//   「色調(ColorGrading)」「放射ブラー」「色収差」「ビネット」へ毎フレーム反映する
//   ウィッチタイム演出のドライバ。
//
// 設計方針:
//   - エンティティ走査は不要。singleton 設定だけを触るため SystemBase を直接継承
//     （PostProcessSystem / ScreenImpactSystem と同じ流儀）。
//   - PostProcessSystem(R09) より前に走る必要があるため R02_Environment に登録。
//   - 立ち上がり(rising edge)で「そのシーンが元々持っていた ColorGrading / Vignette の値」を
//     スナップショットし、立ち下がりで完全に復元する。これにより演出が終わっても
//     ユーザーがシーンに設定した色調を壊さない。
//   - 放射ブラー/色収差は既定 enable=false なので、ScreenImpact と同様に立ち下がりで OFF。
//   - timer が 0 の間は何も書かない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <DirectXMath.h>

/// @brief ジャスト回避演出（ウィッチタイムのPP群）を駆動するシステム。
class JustEvadeEffectSystem : public CCL::ECS::SystemBase
{
public:
    JustEvadeEffectSystem() : SystemBase("JustEvadeEffectSystem") {}

    void Update(float dt) override;

private:
    bool _wasActive = false; ///< 前フレームで演出が出ていたか（立ち上がり/下がり検出）

    // 立ち上がりで退避する「元の」PP値（立ち下がりでここへ完全復元する）。
    struct Baseline
    {
        // ColorGrading
        bool                 cgEnable   = true;
        float                cgSat      = 1.0f;
        float                cgContrast = 1.0f;
        float                cgBright   = 1.0f;
        DirectX::XMFLOAT4    cgTint     = {1.0f, 1.0f, 1.0f, 1.0f};
        float                cgMix      = 1.0f;
        // Vignette
        bool                 vigEnable    = true;
        float                vigIntensity = 0.4f;
        DirectX::XMFLOAT4    vigColor     = {0.0f, 0.0f, 0.0f, 1.0f};
        float                vigPulse     = 0.0f;
    };
    Baseline _base;
    float    _vigTime = 0.0f; ///< ビネット脈動用の位相（演出中に加算）
};
