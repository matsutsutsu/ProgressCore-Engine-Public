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
//   - 放射ブラー/色収差は「自分のスロット(burstJustEvade)」だけを書く。設定本体は
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
    bool m_wasActive = false; ///< 前フレームで演出が出ていたか（立ち上がり/下がり検出）

    // 立ち上がりで退避する「元の」PP値（立ち下がりでここへ完全復元する）。
    struct Baseline
    {
        // ColorGrading
        bool                 cgEnable   = true;
        float                cgSat      = 1.0f;
        float                cgContrast = 1.0f;
        float                cgBright   = 1.0f;
        DirectX::XMFLOAT4    cgTint     = {1.0f, 1.0f, 1.0f, 1.0f};
        float                cgMix      = 1.0f;
        // Vignette
        bool                 vigEnable    = true;
        float                vigIntensity = 0.4f;
        DirectX::XMFLOAT4    vigColor     = {0.0f, 0.0f, 0.0f, 1.0f};
        float                vigPulse     = 0.0f;
        float                vigRadius    = 0.55f; ///< ★他システム(被弾フラッシュ)が触っても確実に戻せるよう退避
        float                vigSoftness  = 0.35f;
    };
    Baseline m_base;
    float    m_vigTime = 0.0f; ///< ビネット脈動用の位相（演出中に加算）
};
