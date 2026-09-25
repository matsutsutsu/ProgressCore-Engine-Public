#pragma once
// ===========================================================================
// 概要: Transform を持つのに TimeState を持たない Entity へ、TimeState を自動で付ける System。
//
// 責務:
//   - TimeState の付け忘れを実行時に補い、警告ログでプレハブの修正を促す
//
// 備考: あくまで設定漏れの救済。実行中に付与が頻発すると Archetype 遷移が増えて
//       Chunk が細切れになるので、エディタ側でプレハブに TimeState を付けておくこと。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h" // UI を対象外にするための判別
#include "TimeState.h"

class TimeScaleSetupSystem : public CCL::ECS::IfSystem<TimeScaleSetupSystem,
                                                       CCL::ECS::Read<TransformComponent>>
{
public:
    TimeScaleSetupSystem() : IfSystem("TimeScaleSetupSystem")
    {
        // 既に TimeState を持つものは対象外。
        // UI は時間スケール（ヒットストップ・スロー）の影響を受けてほしくないので対象外
        SetExclude<TimeStateComponent, UITransformComponent>();
    }

    virtual ~TimeScaleSetupSystem() = default;

    void Update(float dt) override;
};
