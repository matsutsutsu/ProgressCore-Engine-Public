#pragma once
// ===========================================================================
// ScreenImpactSystem.h
// --------------------
// 役割:
//   ScreenImpactComponent(singleton) の timer を減衰させ、その強さを
//   「カメラシェイク」と「放射ブラー/色収差のバースト」へ毎フレーム反映する。
//
// 設計方針:
//   - エンティティ走査は不要。singleton 設定と Camera リソースだけを触るため
//     SystemBase を直接継承する（PostProcessSystem と同じ流儀）。
//   - CameraBrainSystem(R01b) が MainCamera を確定した「後」に走る必要があるため
//     R02_Environment に登録する。放射ブラー/色収差の設定書き込みは、それを読む
//     PostProcessSystem(R09) より前なので同フレームで反映される。
//   - PP へは「自分のキーで確保したスロット」だけを書く。設定本体(enable/data)は
//     シーンの持ち物なので触らない。最終強度の合成と描画ON/OFFの判断は
//     PostProcessSystem が行う。これにより、同時に走る他の演出
//     （ジャスト回避スパイク・被弾フラッシュ）と値を奪い合わない。
//   - timer が 0 の間は何も書かない。立ち下がりで自分のスロットだけ 0 に戻す。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Game/Logics/Effects/Components/ScreenImpactComponent.h"
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"

/// @brief 命中演出（カメラ揺れ＋PPバースト）を駆動するシステム。
class ScreenImpactSystem : public CCL::ECS::SystemBase
{
public:
    ScreenImpactSystem() : SystemBase("ScreenImpactSystem") {}

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        return {CCL::ECS::TypeInfo<ScreenImpactComponent>::ID()};
    }

    /// @note PP 設定は読むだけでなく書き換えるので Write として申告する。
    ///       Read のままだと、同じ PP を書く他システムと同一バッチに置かれて
    ///       並列実行され、書き込みが競合する。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        return {CCL::ECS::TypeInfo<RadialBlurConfigComponent>::ID(),
                CCL::ECS::TypeInfo<ChromaticAberrationConfigComponent>::ID()};
    }

    void Update(float dt) override;

private:
    bool m_wasActive = false; ///< 前フレームで演出が出ていたか（立ち下がりでPP OFF）
};
