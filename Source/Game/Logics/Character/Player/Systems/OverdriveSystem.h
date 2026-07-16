#pragma once
/// @file OverdriveSystem.h
/// @brief OVERDRIVE のゲージ蓄積・発動・時間スケール駆動・HUD描画を担うシステム。
///
/// 役割:
///   - ジャスト回避成功(JustEvadeSuccessEvent)でゲージを多め、ダッシュ開始で僅かに加算。
///   - ゲージMAX時に発動入力("Overdrive")で発動。持続中は時間経過でゲージ減少。
///   - 発動中は TimeContext の overdrive スケールを設定し、「敵はスロー・自分は高速」を作る。
///   - OnGui でゲージHUD（バー＋状態表示）を前面に描画する。
///
/// 位置付け:
///   - 時間スケールは TimeContext(overdrivePlayerScale/overdriveEnemyScale) 経由で
///     TimeScaleSystem(L01) が実効dtへ反映する（本システムはL02なので1フレーム遅延で反映＝体感無問題）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/OverdriveComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"

class OverdriveSystem : public CCL::ECS::IfSystem<OverdriveSystem,
                            CCL::ECS::Write<OverdriveComponent>,
                            CCL::ECS::Read<DashComponent>>
{
  public:
    OverdriveSystem()
        : IfSystem("OverdriveSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;
    void OnGui() override;
};
