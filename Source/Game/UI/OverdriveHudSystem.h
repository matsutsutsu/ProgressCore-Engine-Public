#pragma once
/// @file OverdriveHudSystem.h
/// @brief OVERDRIVE ゲージHUDを ECS UI で構築し、OverdriveComponent の状態を毎フレーム連動させる。
///
/// 目的（Slice 4 の主眼）:
///   - 旧実装は OverdriveSystem::OnGui が ImGui foreground draw list で直描きしていた。
///     ImGuiはエディタ技術で製品ビルド(ENABLE_EDITORオフ)では消える。
///   - これを「UIエンティティ(UIProgressBar/UIText) ＋ データ連動System」に置換し、
///     製品ビルドでも動く HUD にする（データ連動＝バインドの実証）。
///
/// 役割:
///   - Initialize でゲージ(UITransform+UIProgressBar)とラベル(UITransform+UIText)を生成。
///   - Update で最初の OverdriveComponent を読み、value/色/ラベル文字列へ反映。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/OverdriveComponent.h"

class OverdriveHudSystem : public CCL::ECS::IfSystem<OverdriveHudSystem,
                               CCL::ECS::Read<OverdriveComponent>>
{
  public:
    OverdriveHudSystem()
        : IfSystem("OverdriveHudSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;

  private:
    CCL::ECS::EntityID m_gaugeID = CCL::ECS::InvalidEntityID; ///< ゲージ本体（UIProgressBar）
    CCL::ECS::EntityID m_labelID = CCL::ECS::InvalidEntityID; ///< ラベル（UIText）
};
