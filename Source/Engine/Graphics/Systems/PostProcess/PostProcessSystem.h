#pragma once
// ===========================================================================
// 概要: ポストプロセスの設定コンポーネントを読み、PostProcessManager の各エフェクトへ
//       渡す System（R09_PostProcess）。
//
// 責務:
//   - 各エフェクトのオン/オフとパラメータを毎フレーム設定する
//   - 演出からの上書き（overrides）を合成した値を渡す
//   - ポーズ中の背景ぼかしの量を実時間で進める
//
// 備考: エフェクトはアプリ全体で使い回すので、設定コンポーネントが無いシーンでも
//       毎フレーム「無効」を明示して渡す（Update 内のコメント参照）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Graphics/Components/PostProcess/BloomConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ToneMapConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/DistortionConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/DepthOfFieldConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ColorGradingConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/SSAOConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ContactShadowConfigComponent.h"

class PostProcessManager;

class PostProcessSystem : public CCL::ECS::SystemBase
{
public:
    PostProcessSystem()
        : SystemBase("PostProcessSystem")
    {
    }

    // 読むコンポーネントをスケジューラに申告する
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        return {CCL::ECS::TypeInfo<BloomConfigComponent>::ID(),
                CCL::ECS::TypeInfo<ToneMapConfigComponent>::ID(),
                CCL::ECS::TypeInfo<DistortionConfigComponent>::ID(),
                CCL::ECS::TypeInfo<RadialBlurConfigComponent>::ID(),
                CCL::ECS::TypeInfo<DepthOfFieldConfigComponent>::ID(),
                CCL::ECS::TypeInfo<VignetteConfigComponent>::ID(),
                CCL::ECS::TypeInfo<ChromaticAberrationConfigComponent>::ID(),
                CCL::ECS::TypeInfo<ColorGradingConfigComponent>::ID(),
                CCL::ECS::TypeInfo<SSAOConfigComponent>::ID(),
                CCL::ECS::TypeInfo<ContactShadowConfigComponent>::ID()};
    }

    void Update(float dt) override;

private:
    // ポーズぼかしの今の量（0〜1）。世界の時計は止まっているので、実時間で 0 と 1 の間を動かす
    float m_pauseBlur = 0.0f;

    static constexpr float kPauseBlurInSpeed   = 11.0f;  // ポーズに入るときの追従の速さ
    static constexpr float kPauseBlurOutSpeed  = 20.0f;  // 解除のときの追従の速さ。操作の再開を待たせないよう速くする
    static constexpr float kPauseBlurMaxRadius = 0.006f; // サンプルする円の最大半径（UV）
};
