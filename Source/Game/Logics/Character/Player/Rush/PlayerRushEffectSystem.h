#pragma once
// ===========================================================================
// 概要: ラッシュの画面演出を、ビネットと放射ブラーへ毎フレーム反映する System
//
// 責務:
//   - ラッシュ中か（isActive）・範囲外の回避のスロー中か（evadeSlowTimer）に合わせて、weight を実時間でフェードする
//   - 自分のスロットを通してビネットと放射ブラーへ書き、消えたら手放す
//
// 備考: 設定はプレイヤーが持つ。画面は1つなので、プレイヤーが1人である前提
//       強さの合成は PostProcessSystem が行うので、ほかの演出と値を取り合わない
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushEffectComponent.h"

#include <vector>

class PlayerRushEffectSystem : public CCL::ECS::IfSystem<
    PlayerRushEffectSystem,
    CCL::ECS::Write<PlayerRushEffectComponent>,
    CCL::ECS::Read<PlayerRushComponent>>
{
public:
    PlayerRushEffectSystem() : IfSystem("PlayerRushEffectSystem") {}

    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // ScreenImpactSystem と同じ設定を書くので、順番を決めておく
    // 結果は順番に依らないが、決めないと起動時に順序不定の警告が出る
    std::vector<const char *> GetRunAfter() const override { return {"ScreenImpactSystem"}; }

    void Update(float dt) override;

private:
    using Base = CCL::ECS::IfSystem<
        PlayerRushEffectSystem,
        CCL::ECS::Write<PlayerRushEffectComponent>,
        CCL::ECS::Read<PlayerRushComponent>>;
};
