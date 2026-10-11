#pragma once
// ===========================================================================
// 概要: JustEvadeEffectComponent の weight を実時間でフェードし、ビネット（周囲の暗がり）へ
//       毎フレーム反映する System。演出はビネット1本に集約している。
//
// 責務:
//   - active に応じて weight を fadeIn/fadeOut 時間でイーズする
//   - ビネットの設定に、自分のキーで取ったスロットを通して値を書く
//   - 演出が消えている間は自分のスロットを手放す
//
// 備考: シングルトンだけを触り、エンティティを回さないので SystemBase を直接継承する。
//       ポストプロセスの設定本体は書き換えない。最終的な強さの合成と描画の有無は
//       PostProcessSystem が決めるので、同時に走る命中の演出や被弾フラッシュと値を取り合わない。
//       PostProcessSystem（R09）より前に走る必要があるので R02_Environment に登録する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <DirectXMath.h>

class JustEvadeEffectSystem : public CCL::ECS::SystemBase
{
public:
    JustEvadeEffectSystem() : SystemBase("JustEvadeEffectSystem") {}

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

    // 触るポストプロセスの設定はすべて書くので Write で申告する（並列実行での競合を防ぐ）
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // ScreenImpactSystem と同じステージで同じ設定を書くので、順番を決めておく。
    // スロットが別なので結果は順番に依らないが、決めないと順序不定の警告が出る
    std::vector<const char *> GetRunAfter() const override { return {"ScreenImpactSystem"}; }

    void Update(float dt) override;
};
