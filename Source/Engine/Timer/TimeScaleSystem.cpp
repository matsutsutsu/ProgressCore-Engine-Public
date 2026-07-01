#include "TimeScaleSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h" // 優先度Enumへのパスは適宜調整してください
#include <algorithm>
#include <imgui.h>

TimeScaleSystem::TimeScaleSystem() : IfSystem("TimeScaleSystem")
{
}

void TimeScaleSystem::Initialize()
{
    hasGui = true;
}

void TimeScaleSystem::Update(float rawDt)
{
    if (!_world || !_world->HasResource<TimeContext>()) return;
    auto& ctx = _world->GetResource<TimeContext>();

    // =========================================================
    // ★ デバッグ：世界時間の凍結とコマ送り
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

    updateSlowMotion(ctx.playerSlow);
    updateSlowMotion(ctx.enemySlow);
    updateSlowMotion(ctx.worldSlow);

    // 計算したスロー倍率を配列（groupScales）に書き込む
    ctx.groupScales[static_cast<size_t>(TimeGroup::Player)] = ctx.playerSlow.currentComputedScale;
    ctx.groupScales[static_cast<size_t>(TimeGroup::Enemy)] = ctx.enemySlow.currentComputedScale;
    // UIやEnvironmentなど、必要に応じて追加可能

    // =========================================================
    // 2. 親時計（世界全体）のヒットスロー計算
    // =========================================================
    if (ctx.globalHitStopTimer > 0.0f) {
        ctx.globalHitStopTimer -= rawDt;
        if (ctx.globalHitStopTimer <= 0.0f) {
            ctx.globalHitStopTimer = 0.0f;
            ctx.globalScale = ctx.baseGlobalScale;
        }
        else {
            ctx.globalScale = ctx.baseGlobalScale * ctx.globalFreezeScale;
        }
    }
    else {
        ctx.globalScale = ctx.baseGlobalScale;
    }

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
        // ★最適化：switch文による分岐予測ペナルティを排除し、O(1)の配列アクセスで取得
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
    if (!_world || !_world->HasResource<TimeContext>()) return;
    auto& ctx = _world->GetResource<TimeContext>();

    ImGui::SeparatorText("World Time Master");
    ImGui::DragFloat("Base Global Scale", &ctx.baseGlobalScale, 0.01f, 0.0f, 5.0f, "%.3f");

    ImGui::SeparatorText("Faction Scales (groupScales)");
    // 配列の値を直接いじれるようにする
    ImGui::DragFloat("Player Scale", &ctx.groupScales[static_cast<size_t>(TimeGroup::Player)], 0.01f, 0.0f, 3.0f, "%.2f");
    ImGui::DragFloat("Enemy Scale", &ctx.groupScales[static_cast<size_t>(TimeGroup::Enemy)], 0.01f, 0.0f, 3.0f, "%.2f");

    ImGui::SeparatorText("Live Status");
    ImGui::Text("Active Global Scale : %.3f", ctx.globalScale);
    ImGui::Text("HitStop Timer       : %.3f sec", ctx.globalHitStopTimer);

    ImGui::SeparatorText("Debug Actions");
    if (ImGui::Button("Trigger Witch Time (2.0s, 10% Speed)")) {
        ctx.globalHitStopTimer = 2.0f;
        ctx.globalFreezeScale = 0.1f;
    }

    // デバッグ用のコマ送り機能
    ImGui::SeparatorText("Debug Pause & Step");
    ImGui::Checkbox("Pause Game (F10)", &ctx.isPaused);
    if (ctx.isPaused)
    {
        if (ImGui::Button("Step 1 Frame (F11)"))
        {
            ctx.stepNextFrame = true;
        }
    }
}

REGISTER_LOGIC_SYSTEM(TimeScaleSystem, Priority::LogicStage::L01_TimeScale);