#pragma once
// ===========================================================================
// JustEvadeEffectSystem.h
// -----------------------
// 役割:
//   JustEvadeEffectComponent(singleton) の timer を実時間で減衰させ、その強さを
//   「色調(ColorGrading)」「放射ブラー」「色収差」「ビネット」へ毎フレーム反映する
//   ウィッチタイム演出のドライバ。
//
// 設計方針:
//   - エンティティ走査は不要。singleton 設定だけを触るため SystemBase を直接継承
//     （PostProcessSystem / ScreenImpactSystem と同じ流儀）。
//   - PostProcessSystem(R09) より前に走る必要があるため R02_Environment に登録。
//   - 立ち上がり(rising edge)で「そのシーンが元々持っていた ColorGrading / Vignette の値」を
//     スナップショットし、立ち下がりで完全に復元する。これにより演出が終わっても
//     ユーザーがシーンに設定した色調を壊さない。
//   - PP へは「自分のキーで確保したスロット」だけを書く。設定本体は
//     触らず、最終強度の合成と描画ON/OFFは PostProcessSystem に任せる。
//     こうしないと、同時に走る命中バーストや被弾フラッシュと値を奪い合う。
//   - timer が 0 の間は何も書かない。立ち下がりで自分のスロットだけ 0 に戻す。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <DirectXMath.h>

/// @brief ジャスト回避演出（ウィッチタイムのPP群）を駆動するシステム。
class JustEvadeEffectSystem : public CCL::ECS::SystemBase
{
public:
    JustEvadeEffectSystem() : SystemBase("JustEvadeEffectSystem") {}

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

    /// @note 触る PP 設定はすべて書き換えるので Write として申告する（並列実行の競合防止）。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    /// @note ScreenImpactSystem と同一ステージで同じ PP を書くため、順序を確定させる。
    ///       スロットが別なので結果は順序に依存しないが、順序不定の警告を出さないため。
    std::vector<const char *> GetRunAfter() const override { return {"ScreenImpactSystem"}; }

    void Update(float dt) override;

private:
    /// @brief 前フレームで演出が出ていたか（立ち下がりで自分のスロットを手放すため）。
    /// @note  素の PP 設定を退避するメンバは持たない。各スロットへ「目標値と weight」を
    ///        書くだけで、素の設定との合成は PostProcessSystem の Resolve() が行う。
    bool m_wasActive = false;
};
