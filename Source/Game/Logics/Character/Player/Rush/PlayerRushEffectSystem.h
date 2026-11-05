#pragma once
// ===========================================================================
// 概要: ラッシュの画面演出を、ビネットと放射ブラーへ毎フレーム反映する System
//
// 責務:
//   - rushActive に合わせて、weight を実時間でフェードする
//   - 自分のスロットを通してビネットと放射ブラーへ書き、消えたら手放す
//
// 備考: シングルトンだけを触り、エンティティを回さないので SystemBase を直接継承する
//       強さの合成は PostProcessSystem が行うので、ほかの演出と値を取り合わない
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <DirectXMath.h>

class PlayerRushEffectSystem : public CCL::ECS::SystemBase
{
public:
    PlayerRushEffectSystem() : SystemBase("PlayerRushEffectSystem") {}

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // ScreenImpactSystem と同じ設定を書くので、順番を決めておく
    // 結果は順番に依らないが、決めないと起動時に順序不定の警告が出る
    std::vector<const char *> GetRunAfter() const override { return {"ScreenImpactSystem"}; }

    void Update(float dt) override;
};
