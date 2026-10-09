#pragma once
// ===========================================================================
// 概要: ChargeMode から突撃中の動き方（IChargeMotion）を引く表。
//
// 責務:
//   - ChargeMode の値ごとに、動き方のインスタンスを1つずつ持つ
//   - 値から動き方を引く
//
// 備考:
//   ChargeMode の値は数個しかないので、陣形の FormationRegistry のような登録の仕組みは持たず、
//   .cpp の固定の表で引く。動き方を足すときは、ChargeMode に値を足し、.cpp の表に1行足す。
// ===========================================================================
#include "Game/Logics/AI/Drone/Motion/IChargeMotion.h"

namespace CCL::AI::Motion
{
    // chargeMode の動き方を返す。表に無い値は直進（Straight）として扱う。
    const IChargeMotion &GetChargeMotion(ChargeMode mode);

    // 全部の動き方に、フレームの始まりを伝える（IChargeMotion::BeginFrame）。並列ループの前に1回呼ぶ。
    void BeginChargeMotionsFrame(CCL::ECS::World &world);

} // namespace CCL::AI::Motion
