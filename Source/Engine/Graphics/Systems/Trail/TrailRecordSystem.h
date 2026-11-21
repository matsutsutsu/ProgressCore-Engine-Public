#pragma once
// ===========================================================================
// 概要: トレイルの時間をロジックで進め、描画側へ「1区間刻んでいい」印を出す System
//
// 責務:
//   - 断面の age を進め、lifeTime を超えたものを古い順に捨てる
//   - 一時トレイルのタイマーを進め、切れたら emitting を落とす
//   - 老化に使った時間の倍率を timeScale に書く
//   - emitting なら sampleRequested を立てる
//
// 備考: L06_Resolution で走る
//       刻む頻度をロジックに合わせるのは、剣の姿勢が 60Hz でしか変わらないため
//       描画のレートで刻むと、同じ姿勢の断面がずれて重なり、尾が二重に見える
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Graphics/Components/Trail/TrailComponent.h"

class TrailRecordSystem : public CCL::ECS::IfSystem<
    TrailRecordSystem,
    CCL::ECS::Write<TrailComponent>>
{
public:
    TrailRecordSystem() : IfSystem("TrailRecordSystem") {}

    void Update(float dt) override;

private:
    // Entity の時計（localDt）で、このステップに進む時間を返す
    // TimeStateComponent がなければ fallbackDt
    float ClockDt(CCL::ECS::EntityID id, float fallbackDt) const;

    // 世界全体の倍率を掛けた dt（秒）。一時停止中は 0
    float WorldDt(float dt) const;
};
