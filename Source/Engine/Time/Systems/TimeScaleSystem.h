#pragma once
// ===========================================================================
// 概要: 全 Entity の実効 dt（TimeStateComponent::localDt）を毎ステップ計算する System。
//
// 責務:
//   - 世界全体の倍率（ヒットストップ・シネマティックスロー）を TimeContext に計算する
//   - 各 Entity の localDt = rawDt × 全体 × グループ × バフ × 演出 を確定する
//   - 一時停止・コマ送り（TimeContext.isPaused / stepNextFrame）
//
// 備考: 倍率の「入れ方」は Time ファサード（TimeService.h）が提供する。
//       ここは入った値から dt を合成するだけ。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"

class TimeScaleSystem : public CCL::ECS::IfSystem<TimeScaleSystem, CCL::ECS::Write<TimeStateComponent>>
{
public:
    TimeScaleSystem();

    // rawDt はスケール前の固定ステップ時間
    void Update(float rawDt) override;

    void OnGui() override;
};
