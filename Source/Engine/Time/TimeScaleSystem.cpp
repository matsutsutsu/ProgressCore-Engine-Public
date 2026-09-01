#include "TimeScaleSystem.h"
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
    auto& ctx = m_world->GetResource<TimeContext>();

    // =========================================================
    // デバッグ：世界時間の凍結とコマ送り
    // =========================================================
    if (ctx.isPaused)
    {
        if (ctx.stepNextFrame)
        {
            // 1フレーム(60fps想定)だけ強制的に時間を進める
            rawDt             = 0.016666f;
            ctx.stepNextFrame = false;
        }
        else
        {
            // 完全に時間を止める
            rawDt = 0.0f;
        }
    }

    // =========================================================
    // 1. SlowMotionデータの更新（ロジックの分離）
    // =========================================================
    auto updateSlowMotion = [rawDt](SlowMotionData& slow) {
        if (slow.timer > 0.0f) {
            slow.timer -= rawDt;
            float progress = 1.0f - ((std::max)(slow.timer / slow.duration, 0.0f));
            float easeOut = 1.0f - (1.0f - progress) * (1.0f - progress);
            slow.currentComputedScale = std::lerp(slow.targetScale, 1.0f, easeOut);
        }
        else {
            slow.timer = 0.0f;
            slow.currentComputedScale = 1.0f;
        }
        };

    // groupScale レイヤーは現状どの system も書かない（全て1.0）。将来 Time::SlowGroup 等を
    // 足す時は「毎フレーム 1.0 へリセット → min で要求を合成」の形でここに層を用意する。

    // =========================================================
    // 2. 親時計（世界全体）の計算
    //    ① 全体ヒットストップ：命中の一瞬停止（イーズ無し＝キビキビ）
    //    ② シネマティックスロー：撃破スロー等（イーズ有り＝滑らかに戻す）
    //    どちらも globalScale に乗るので、group に関係なく全エンティティへ効く。
    // =========================================================
    float hitStopFactor = 1.0f;
    if (ctx.globalHitStopTimer > 0.0f) {
        ctx.globalHitStopTimer -= rawDt;
        if (ctx.globalHitStopTimer <= 0.0f)
            ctx.globalHitStopTimer = 0.0f;
        else
            hitStopFactor = ctx.globalFreezeScale;
    }

    // 撃破スロー等のシネマティックスローをイーズしながら 1.0 へ戻す。
    updateSlowMotion(ctx.cinematicSlow);

    ctx.globalScale = ctx.baseGlobalScale * hitStopFactor * ctx.cinematicSlow.currentComputedScale;

    // =========================================================
    // 3. 【マルチスレッド超並列実行】各自の時計の更新
    // =========================================================
    ForEachParallel([&](TimeStateComponent& time) {

        // 【レイヤー1】 個別の完全停止（ヒットストップ）最優先
        if (time.hitStopTimer > 0.0f) {
            time.hitStopTimer -= rawDt;
            if (time.hitStopTimer <= 0.0f) {
                time.hitStopTimer = 0.0f;
            }
            else {
                // 停止中はバフ等の倍率を無視して強制上書き（即return）
                time.localDt = rawDt * time.freezeScale;
                return;
            }
        }

        // 【レイヤー2 & 3】 スケールの合成
        // 最適化：switch文による分岐予測ペナルティを排除し、O(1)の配列アクセスで取得
        float groupScale = ctx.groupScales[static_cast<size_t>(time.group)];

        // 各種スケールを乗算して最終的な実効dtを確定
        float finalScale = ctx.globalScale * groupScale * time.buffScale * time.cinematicScale;
        time.localDt = rawDt * finalScale;
        });
}

// ============================================================================
// デバッガ・GUI
// ============================================================================
void TimeScaleSystem::OnGui()
{
    if (!m_world || !m_world->HasResource<TimeContext>()) return;
    auto& ctx = m_world->GetResource<TimeContext>();

    ImGui::SeparatorText("World Time Master");
    ImGui::DragFloat("Base Global Scale", &ctx.baseGlobalScale, 0.01f, 0.0f, 5.0f, "%.3f");

    // 出力（TimeScaleSystem が算出）は読み取り専用で見せる。直接いじると翌フレーム上書きされるため。
    ImGui::SeparatorText("Live Status (read-only)");
    ImGui::Text("Global Scale   : %.3f", ctx.globalScale);
    ImGui::Text("HitStop Timer  : %.3f sec", ctx.globalHitStopTimer);
    ImGui::Text("Cinematic Slow : x%.2f (t=%.2f)", ctx.cinematicSlow.currentComputedScale, ctx.cinematicSlow.timer);

    // 操作は生フィールド直書きではなく Time ファサード経由（実運用と同じ経路）。
    ImGui::SeparatorText("Debug Actions (Time facade)");
    if (ImGui::Button("HitStop 0.15s"))          Time::HitStop(*m_world, 0.15f, 0.0f);
    ImGui::SameLine();
    if (ImGui::Button("Witch Time (2s @10%)"))   Time::HitStop(*m_world, 2.0f, 0.1f);
    if (ImGui::Button("Slow World (1s @25%)"))   Time::SlowWorld(*m_world, 0.25f, 1.0f);
    ImGui::SameLine();
    if (ImGui::Button("Clear Slow"))             Time::ClearWorldSlow(*m_world);

    ImGui::SeparatorText("Pause & Step");
    bool paused = ctx.isPaused;
    if (ImGui::Checkbox("Pause Game (F10)", &paused))
        paused ? Time::Pause(*m_world) : Time::Resume(*m_world);
    if (ctx.isPaused && ImGui::Button("Step 1 Frame (F11)"))
        Time::StepFrame(*m_world);
}

REGISTER_LOGIC_SYSTEM(TimeScaleSystem, Priority::LogicStage::L01_TimeScale);
