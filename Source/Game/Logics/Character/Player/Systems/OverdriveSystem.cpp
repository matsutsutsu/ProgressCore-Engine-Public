/// @file OverdriveSystem.cpp
/// 役割・設計方針は OverdriveSystem.h を参照。
#include "OverdriveSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Time/TimeState.h"
#include "Game/Logics/GameEvent/GameEvents.h"
#include "Engine/Platform/Input/IInputAPI.h"

#include <memory>
#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::ECS;

void OverdriveSystem::Initialize()
{
    // ジャスト回避成功でゲージを多めに加算（近道）。発動中は加算しない（無限延長防止）。
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        if (auto *od = m_world->GetComponent<OverdriveComponent>(ev.playerID))
        {
            if (!od->isActive)
                od->AddGauge(od->gainOnJustEvade);
        }
    });
}

void OverdriveSystem::Update(float dt)
{
    // 発動入力（Triggered：押した瞬間だけ true）。
    bool activatePressed = false;
    if (m_world->HasResource<std::shared_ptr<IInputAPI>>())
    {
        auto input      = m_world->GetResource<std::shared_ptr<IInputAPI>>();
        activatePressed = input->GetAction("Overdrive");
    }

    // 非発動時は等倍。発動しているプレイヤーがいればその倍率を採用する。
    float enemyScale       = 1.0f;
    float environmentScale = 1.0f;
    float playerScale      = 1.0f;

    ForEachWithID([&](EntityID /*id*/, OverdriveComponent &od, const DashComponent &dash) {
        // 1. ダッシュ開始（立ち上がり）で僅かに加算＝通常回避の救済ルート。
        if (!od.isActive && dash.isDashing && !od.wasDashing)
            od.AddGauge(od.gainOnDash);
        od.wasDashing = dash.isDashing;

        // 2. ゲージMAXで任意発動。
        if (activatePressed && od.IsFull() && !od.isActive)
            od.isActive = true;

        // 3. 発動中は時間経過でゲージ減少。0でOVERDRIVE終了。
        if (od.isActive)
        {
            const float drain = (od.duration > 0.0f) ? (od.maxGauge / od.duration) : od.maxGauge;
            od.currentGauge -= drain * dt;
            if (od.currentGauge <= 0.0f)
            {
                od.currentGauge = 0.0f;
                od.isActive     = false;
            }
        }

        // 4. 発動中の時間スケール（敵＆環境スロー・自分高速）を採用。
        if (od.isActive)
        {
            enemyScale       = od.enemyTimeScale;
            environmentScale = od.environmentTimeScale;
            playerScale      = od.playerTimeScale;
        }
    });

    // 勢力別の追加倍率を TimeContext へ。TimeScaleSystem(L01) が実効dtへ折り込む。
    if (m_world->HasResource<TimeContext>())
    {
        auto &ctx                     = m_world->GetResource<TimeContext>();
        ctx.overdriveEnemyScale       = enemyScale;
        ctx.overdriveEnvironmentScale = environmentScale;
        ctx.overdrivePlayerScale      = playerScale;
    }
}

// ゲージHUDは ECS UI（OverdriveHudSystem + UIProgressBar/UIText）へ移行済み。
// 旧 ImGui foreground 直描きは撤去（製品ビルドで消えないHUDにするため）。OnGui は未使用。
void OverdriveSystem::OnGui()
{
}

// 時間スケールは1フレーム遅延で TimeScaleSystem に折り込まれる（体感無問題）。
REGISTER_LOGIC_SYSTEM(OverdriveSystem, Priority::LogicStage::L02_PreUpdate);
