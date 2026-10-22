#pragma once
// ===========================================================================
// 概要: 突撃中（FireCharge）の動き方3種と、ChargeMode から動き方を引く表の入口。
//
// 責務:
//   - ChargeMode から動き方（IChargeMotion）を引く
//   - 全部の動き方にフレームの始まりを伝える
//   - 直進の突撃の始め方（BeginStraightCharge）を提供する
//
// 備考:
//   動き方の本体は ChargeMotions.cpp の中にだけある。
//     Straight    … 弾幕。タメ → 直進。タメの間は当たらない
//     RigidFollow … データ陣形（縦割り・挟み込み・横なぎ等）。持ち場に毎フレーム一致させ、形が歪まない
//     SoftFollow  … 小隊突撃。持ち場へバネで寄る。速いほど後ろへ流れ、隊列がしなる
//
//   外から使うのはこのヘッダの関数だけなので、クラスは .cpp に隠してある。
//   動き方を足すときは、ChargeMode に値を足し、.cpp にクラスを書いて表に1行足す。
// ===========================================================================
#include "Game/Logics/AI/Drone/Motion/IChargeMotion.h"

namespace CCL::AI::Motion
{
    // chargeMode の動き方を返す。表に無い値は直進（Straight）として扱う。
    const IChargeMotion &GetChargeMotion(ChargeMode mode);

    // 全部の動き方に、フレームの始まりを伝える（IChargeMotion::BeginFrame）。並列ループの前に1回呼ぶ。
    void BeginChargeMotionsFrame(CCL::ECS::World &world);

    /**
     * @brief 直進の突撃を始める。FireCharge へ入った瞬間に1回だけ呼ぶ。
     *
     * 移動方式を直進にし、タメの時間を入れ、撃つ方向が無ければ決める。
     * ここで方向を固定するので、タメ→リリースは方向を変えない直進になる（ホーミングしない）。
     *
     * 技の突撃の始め方（IFormation::OnChargeBegin）の既定。技が別の始め方を書かなければ、突撃はここから始まる。
     *
     * @param drone 突撃を始める機体。
     * @param info  プレイヤーの位置など。陣形が撃つ方向を書かなかったときの狙いに使う。
     */
    void BeginStraightCharge(DroneComponent &drone, const ChargeBeginInfo &info);

} // namespace CCL::AI::Motion
