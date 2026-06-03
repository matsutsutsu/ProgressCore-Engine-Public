#include "TimeScaleSystem.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h" // 優先度Enumへのパスは適宜調整してください
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
    // 親時計のリソースが存在しなければ何もしない
    if (!_world || !_world->HasResource<TimeContext>()) return;

    // O(1)で超高速に親時計の情報を取得
    auto& ctx = _world->GetResource<TimeContext>();

    // =========================================================
    //  親時計（世界全体）のヒットスロー計算
    // =========================================================
    if (ctx.globalHitStopTimer > 0.0f) {
        ctx.globalHitStopTimer -= rawDt; // 現実時間で減算

        if (ctx.globalHitStopTimer <= 0.0f) {
            ctx.globalHitStopTimer = 0.0f;
            ctx.globalScale = ctx.baseGlobalScale; // 元の速度に戻る
        }
        else {
            // 全体を超スローにする
            ctx.globalScale = ctx.baseGlobalScale * ctx.globalFreezeScale;
        }
    }
    else {
        // ヒットスローが終わっていれば基本スケールを維持
        ctx.globalScale = ctx.baseGlobalScale;
    }

    float currentSlowModifier = 1.0f;
    // 2. 各勢力個別のスローモーションを更新
    ctx.playerSlow.Update(rawDt);
    ctx.enemySlow.Update(rawDt);
    ctx.worldSlow.Update(rawDt);

    // ★追加: ヒットストップの影響を受けない、滑らかな「演出用スケール」を確定させる
    // これをオーディオが参照することで、打撃停止のガクガク音を回避します
    ctx.cinematicScale = ctx.baseGlobalScale * ctx.worldSlow.currentComputedScale;

    if (ctx.globalHitStopTimer > 0.0f) {
        ctx.globalHitStopTimer -= rawDt;
        if (ctx.globalHitStopTimer <= 0.0f) {
            ctx.globalHitStopTimer = 0.0f;
            ctx.globalScale = ctx.cinematicScale; // ヒットストップ解除時は演出スケールに復帰
        }
        else {
            ctx.globalScale = ctx.cinematicScale * ctx.globalFreezeScale;
        }
    }
    else {
        ctx.globalScale = ctx.cinematicScale;
    }

    // 3. 最終的な勢力スケールを確定
    ctx.playerScale = ctx.playerSlow.currentComputedScale;
    ctx.enemyScale = ctx.enemySlow.currentComputedScale;
    // 必要に応じてカメラ用スケールなどを設定
    ctx.cameraScale = 1.0f;

    // 【マルチスレッド超並列実行】
    // チャンク単位で分割され、全コアを使って腕時計の時間を一斉に合わせる
    ForEachParallel([&](TimeState& time) {

        // 1. 個別のヒットストップ処理
        if (time.hitStopTimer > 0.0f) {
            time.hitStopTimer -= rawDt; // 実際の時間で減算

            if (time.hitStopTimer <= 0.0f) {
                time.hitStopTimer = 0.0f; // 停止終了
            }
            else {
                // ヒットストップ中のdtを計算して即終了（スケール計算はスキップ）
                time.localDt = rawDt * time.freezeScale;
                return;
            }
        }

        // 2. 勢力ごとのスローモーション計算
        float scale = ctx.globalScale;
        switch (time.group) {
        case TimeGroup::Player:      scale *= ctx.playerScale; break;
        case TimeGroup::Enemy:       scale *= ctx.enemyScale;  break;
        case TimeGroup::Environment: /* 環境用スケールがあれば乗算 */ break;
        default: break;
        }

        // 3. 今フレームの最終的な実効dtを確定
        time.localDt = rawDt * scale;
        });
}


// ============================================================================
// ★追加：リアルタイム・タイムスケール・デバッガ
// ============================================================================
void TimeScaleSystem::OnGui()
{
    if (!_world || !_world->HasResource<TimeContext>()) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "TimeContext Resource is missing!");
        return;
    }

    auto& ctx = _world->GetResource<TimeContext>();

    // --- 1. 世界全体のマスター時計 ---
    ImGui::SeparatorText("World Time Master");
    // ゲームプレイ中に0.1にすれば、マトリックスのようなスローモーションで挙動を観察できます
    ImGui::DragFloat("Base Global Scale", &ctx.baseGlobalScale, 0.01f, 0.0f, 5.0f, "%.3f");

    // --- 2. 勢力別の個別スケール ---
    ImGui::SeparatorText("Faction Scales");
    // プレイヤーだけを2倍速にしたり、敵だけを半分の速度にしたりしてバランスをテストできます
    ImGui::DragFloat("Player Scale", &ctx.playerScale, 0.01f, 0.0f, 3.0f, "%.2f");
    ImGui::DragFloat("Enemy Scale", &ctx.enemyScale, 0.01f, 0.0f, 3.0f, "%.2f");
    ImGui::DragFloat("Camera Scale", &ctx.cameraScale, 0.01f, 0.0f, 3.0f, "%.2f");

    // --- 3. 動的ステータスのモニタリング ---
    ImGui::SeparatorText("Live Status (Read Only)");
    ImGui::Text("Active Global Scale : %.3f", ctx.globalScale);
    ImGui::Text("HitStop Timer       : %.3f sec", ctx.globalHitStopTimer);

    // --- 4. デバッグ用強制発火ボタン ---
    ImGui::SeparatorText("Debug Actions");
    if (ImGui::Button("Trigger Witch Time (2.0s, 10% Speed)")) {
        ctx.globalHitStopTimer = 2.0f;
        ctx.globalFreezeScale = 0.1f;
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset All")) {
        ctx.baseGlobalScale = 1.0f;
        ctx.playerScale = 1.0f;
        ctx.enemyScale = 1.0f;
        ctx.cameraScale = 1.0f;
        ctx.globalHitStopTimer = 0.0f;
    }
}


// ★ 最重要: 他のどのLogicシステムよりも先に時間を計算するため、PrePhysics等の最も早い段階に登録する
REGISTER_LOGIC_SYSTEM(TimeScaleSystem, Priority::LogicStage::L01_TimeScale)
