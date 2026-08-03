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
//   - timer が 0 の間は何も書かない。アクティブ→非アクティブの立ち下がりで
//     一度だけ PP を OFF にし、他システムの PostProcess 利用を邪魔しない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Graphics/Components/Impact/ScreenImpactComponent.h"
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"

/// @brief 命中演出（カメラ揺れ＋PPバースト）を駆動するシステム。
class ScreenImpactSystem : public CCL::ECS::SystemBase
{
public:
    ScreenImpactSystem() : SystemBase("ScreenImpactSystem") {}

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        return {CCL::ECS::TypeInfo<ScreenImpactComponent>::ID(),
                CCL::ECS::TypeInfo<RadialBlurConfigComponent>::ID(),
                CCL::ECS::TypeInfo<ChromaticAberrationConfigComponent>::ID()};
    }

    void Update(float dt) override;

private:
    bool m_wasActive = false; ///< 前フレームで演出が出ていたか（立ち下がりでPP OFF）
};
