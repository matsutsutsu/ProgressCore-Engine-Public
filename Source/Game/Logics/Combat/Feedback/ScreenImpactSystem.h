#pragma once
// ===========================================================================
// 概要: ScreenImpactComponent の timer を減らし、その強さを放射ブラーと色収差へ
//       毎フレーム反映する System
//
// 責務:
//   - 残り時間から強さを求め、自分のキーで取ったスロットへ書く
//   - 演出がない間は自分のスロットを手放す
//
// 備考: シングルトンだけを触り、エンティティを回さないので SystemBase を直接継承する
//       設定本体（enable / data）はシーンのものなので書き換えない。最終的な強さの合成は
//       PostProcessSystem が行うので、ジャスト回避の演出や被弾フラッシュと値を取り合わない
//       画面の揺れは扱わない（DamageResolutionSystem が CameraShakeRequestEvent を出す）
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Game/Logics/Combat/Feedback/ScreenImpactComponent.h"
#include "Engine/Graphics/Components/PostProcess/PostFx.h"

class ScreenImpactSystem : public CCL::ECS::SystemBase
{
public:
    ScreenImpactSystem() : SystemBase("ScreenImpactSystem") {}

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        return {CCL::ECS::TypeInfo<ScreenImpactComponent>::ID()};
    }

    /**
     * @brief テンプレート引数以外に書く型を申告する
     *
     * 後始末に PostFx::ReleaseAll を使うので、上書きを受け付ける全コンポーネントへ書く
     * 申告より広い範囲へ書くと、同じ設定を書く他の System と同じバッチに入って並列に走り、
     * Acquire が同じ空きスロットを奪い合う
     */
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override { return CCL::PostFx::AllWriteTypes(); }

    void Update(float dt) override;
};
