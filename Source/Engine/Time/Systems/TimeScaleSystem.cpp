#include "Engine/Time/Systems/TimeScaleSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h" // 実行ステージの列挙
#include "Engine/Time/TimeService.h"            // Debug GUI から意図ファサードを叩く
#include <algorithm>
#include <imgui.h>

TimeScaleSystem::TimeScaleSystem() : IfSystem("TimeScaleSystem")
{
}

void TimeScaleSystem::Update(float rawDt)
{
    if (!m_world || !m_world->HasResource<TimeContext>()) return;
    auto &ctx = m_world->GetResource<TimeContext>();

    // 一時停止・コマ送り（デバッグ用）
    if (ctx.isPaused)
    {
        if (ctx.stepNextFrame)
        {
            // 1ステップ（60fps 想定）だけ時間を進める
            rawDt             = 0.016666f;
            ctx.stepNextFrame = false;
        }
        else
        {
            rawDt = 0.0f;
        }
    }

    // 1. スローの残り時間を進め、targetScale から 1.0 へ ease-out で戻す
    auto updateSlowMotion = [rawDt](SlowMotionData &slow) {
        if (slow.timer > 0.0f)
        {
            slow.timer -= rawDt;
            float progress            = 1.0f - ((std::max)(slow.timer / slow.duration, 0.0f));
            float easeOut             = 1.0f - (1.0f - progress) * (1.0f - progress);
            slow.currentComputedScale = std::lerp(slow.targetScale, 1.0f, easeOut);
        }
        else
        {
            slow.timer                = 0.0f;
            slow.currentComputedScale = 1.0f;
        }
    };

    // groupScale レイヤーは現在どの System も書かない（全て 1.0）。Time::SlowGroup 等を
    // 足すときは「毎フレーム 1.0 へリセット → min で要求を合成」の形でここに層を用意する。

    // 2. 世界全体の倍率を計算する
    //    - 全体ヒットストップ    : 命中の一瞬停止（イーズ無しで即座に戻す）
    //    - シネマティックスロー  : 撃破スロー等（イーズを掛けて戻す）
    //    どちらも globalScale に乗るので、group に関係なく全 Entity へ効く。
    float hitStopFactor = 1.0f;
    if (ctx.globalHitStopTimer > 0.0f)
    {
        ctx.globalHitStopTimer -= rawDt;
        if (ctx.globalHitStopTimer <= 0.0f)
            ctx.globalHitStopTimer = 0.0f;
        else
            hitStopFactor = ctx.globalFreezeScale;
    }

    // 撃破スロー等のシネマティックスローをイーズしながら 1.0 へ戻す。
    updateSlowMotion(ctx.cinematicSlow);

    ctx.globalScale = ctx.baseGlobalScale * hitStopFactor * ctx.cinematicSlow.currentComputedScale;

    // 3. 各 Entity の localDt を確定する（Entity 同士は独立なので並列に回す）
    ForEachParallel([&](TimeStateComponent &time) {
        // 【レイヤー1】個別の完全停止（ヒットストップ）が最優先
        if (time.hitStopTimer > 0.0f)
        {
            time.hitStopTimer -= rawDt;
            if (time.hitStopTimer <= 0.0f)
            {
                time.hitStopTimer = 0.0f;
            }
            else
            {
                // 停止中はバフ等の倍率を無視して上書きし、ここで終える
                time.localDt = rawDt * time.freezeScale;
                return;
            }
        }

        // 【レイヤー2 & 3】倍率の合成。グループ倍率は switch ではなく配列で引く
        float groupScale = ctx.groupScales[static_cast<size_t>(time.group)];

        // 全倍率を掛けて実効 dt を確定する
        float finalScale = ctx.globalScale * groupScale * time.buffScale * time.cinematicScale;
        time.localDt     = rawDt * finalScale;
    });
}

void TimeScaleSystem::OnGui()
{
    if (!m_world || !m_world->HasResource<TimeContext>()) return;
    auto &ctx = m_world->GetResource<TimeContext>();

    ImGui::SeparatorText("World Time Master");
    ImGui::DragFloat("Base Global Scale", &ctx.baseGlobalScale, 0.01f, 0.0f, 5.0f, "%.3f");

    // TimeScaleSystem が算出する値は読み取り専用で見せる。直接いじっても翌ステップで上書きされるため
    ImGui::SeparatorText("Live Status (read-only)");
    ImGui::Text("Global Scale   : %.3f", ctx.globalScale);
    ImGui::Text("HitStop Timer  : %.3f sec", ctx.globalHitStopTimer);
    ImGui::Text("Cinematic Slow : x%.2f (t=%.2f)", ctx.cinematicSlow.currentComputedScale, ctx.cinematicSlow.timer);

    // 操作は生フィールドの直書きではなく Time ファサード経由（ゲーム本体と同じ経路）
    ImGui::SeparatorText("Debug Actions (Time facade)");
    if (ImGui::Button("HitStop 0.15s")) Time::HitStop(*m_world, 0.15f, 0.0f);
    ImGui::SameLine();
    if (ImGui::Button("Witch Time (2s @10%)")) Time::HitStop(*m_world, 2.0f, 0.1f);
    if (ImGui::Button("Slow World (1s @25%)")) Time::SlowWorld(*m_world, 0.25f, 1.0f);
    ImGui::SameLine();
    if (ImGui::Button("Clear Slow")) Time::ClearWorldSlow(*m_world);

    ImGui::SeparatorText("Pause & Step");
    bool paused = ctx.isPaused;
    if (ImGui::Checkbox("Pause Game (F10)", &paused))
        paused ? Time::Pause(*m_world) : Time::Resume(*m_world);
    if (ctx.isPaused && ImGui::Button("Step 1 Frame (F11)"))
        Time::StepFrame(*m_world);
}

REGISTER_LOGIC_SYSTEM(TimeScaleSystem, Priority::LogicStage::L01_TimeScale);
