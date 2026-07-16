/// @file OverdriveSystem.cpp
/// 役割・設計方針は OverdriveSystem.h を参照。
#include "OverdriveSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Timer/TimeState.h"
#include "Game/Logics/GameEvent/GameEvents.h"
#include "Engine/Platform/Input/IInputAPI.h"

#include <memory>
#include <imgui.h>

using namespace CCL::ECS;

void OverdriveSystem::Initialize()
{
    hasGui = true; // ゲージHUDを OnGui で前面描画する

    // ジャスト回避成功でゲージを多めに加算（近道）。発動中は加算しない（無限延長防止）。
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        if (auto *od = _world->GetComponent<OverdriveComponent>(ev.playerID))
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
    if (_world->HasResource<std::shared_ptr<IInputAPI>>())
    {
        auto input      = _world->GetResource<std::shared_ptr<IInputAPI>>();
        activatePressed = input->GetAction("Overdrive");
    }

    // 非発動時は等倍。発動しているプレイヤーがいればその倍率を採用する。
    float enemyScale  = 1.0f;
    float playerScale = 1.0f;

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

        // 4. 発動中の時間スケール（敵スロー・自分高速）を採用。
        if (od.isActive)
        {
            enemyScale  = od.enemyTimeScale;
            playerScale = od.playerTimeScale;
        }
    });

    // 勢力別の追加倍率を TimeContext へ。TimeScaleSystem(L01) が実効dtへ折り込む。
    if (_world->HasResource<TimeContext>())
    {
        auto &ctx                = _world->GetResource<TimeContext>();
        ctx.overdriveEnemyScale  = enemyScale;
        ctx.overdrivePlayerScale = playerScale;
    }
}

void OverdriveSystem::OnGui()
{
    // プレイヤーの OverdriveComponent を1つ拾ってHUDを描く。
    const OverdriveComponent *found = nullptr;
    ForEach([&](OverdriveComponent &od, const DashComponent & /*dash*/) {
        if (!found) found = &od;
    });
    if (!found) return;

    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImGuiIO    &io = ImGui::GetIO();

    const float w = 340.0f, h = 24.0f;
    const float x = 48.0f, y = io.DisplaySize.y - 72.0f;

    float ratio = (found->maxGauge > 0.0f) ? (found->currentGauge / found->maxGauge) : 0.0f;
    ratio       = (ratio < 0.0f) ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);

    const ImU32 fill = found->isActive ? IM_COL32(255, 70, 220, 255)   // 発動中：マゼンタ
                     : (found->IsFull() ? IM_COL32(255, 215, 60, 255)   // MAX：ゴールド
                                        : IM_COL32(70, 170, 255, 255));  // 蓄積中：ブルー

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0, 0, 0, 160), 4.0f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w * ratio, y + h), fill, 4.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(255, 255, 255, 210), 4.0f, 0, 2.0f);

    const char *label = found->isActive ? "OVERDRIVE!!"
                      : (found->IsFull() ? "OVERDRIVE READY  [Q]" : "OVERDRIVE");
    dl->AddText(ImVec2(x, y - 20.0f), IM_COL32(255, 255, 255, 255), label);
}

// 時間スケールは1フレーム遅延で TimeScaleSystem に折り込まれる（体感無問題）。
REGISTER_LOGIC_SYSTEM(OverdriveSystem, Priority::LogicStage::L02_PreUpdate);
