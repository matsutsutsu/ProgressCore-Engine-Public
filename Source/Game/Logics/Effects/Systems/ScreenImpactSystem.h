#pragma once
// ===========================================================================
// 概要: ScreenImpactComponent の timer を減らし、その強さを放射ブラーと色収差へ
//       毎フレーム反映する System。
//
// 責務:
//   - 残り時間から強さを求め、自分のキーで取ったスロットへ書く
//   - 演出が終わったフレームで自分のスロットを手放す
//
// 備考: シングルトンだけを触り、エンティティを回さないので SystemBase を直接継承する。
//       設定本体（enable / data）はシーンのものなので書き換えない。最終的な強さの合成は
//       PostProcessSystem が行うので、ジャスト回避の演出や被弾フラッシュと値を取り合わない。
//       画面の揺れは扱わない（DamageResolutionSystem が CameraShakeRequestEvent を出す）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Game/Logics/Effects/Components/ScreenImpactComponent.h"
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"

class ScreenImpactSystem : public CCL::ECS::SystemBase
{
public:
    ScreenImpactSystem() : SystemBase("ScreenImpactSystem") {}

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        return {CCL::ECS::TypeInfo<ScreenImpactComponent>::ID()};
    }

    // ポストプロセスの設定は書くので Write で申告する。Read のままだと、同じ設定を書く
    // 他の System と同じバッチに入って並列に走り、書き込みが競合する
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        return {CCL::ECS::TypeInfo<RadialBlurConfigComponent>::ID(),
                CCL::ECS::TypeInfo<ChromaticAberrationConfigComponent>::ID()};
    }

    void Update(float dt) override;

private:
    bool m_wasActive = false; // 前のフレームで演出を出していたか（終わったフレームでスロットを手放すため）
};
