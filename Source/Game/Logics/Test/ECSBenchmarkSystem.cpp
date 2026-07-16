#include "ECSBenchmarkSystem.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

// 必要なインクルードを追加
#include <imgui.h>
#include "Engine/Timer/TimeState.h"

void ECSBenchmarkSystem::Update(float dt)
{
    // ====================================================================
    // 1. TimeContext の適用 (環境グループの時間を取得)
    // ====================================================================
    if (!_world || !_world->HasResource<TimeContext>()) return;
    auto &ctx = _world->GetResource<TimeContext>();

    // 背景のVFX扱いとするため、Environmentのスケールを適用
    float timeScale = ctx.groupScales[static_cast<size_t>(TimeGroup::Environment)];
    float scaledDt  = dt * timeScale;

    // ポーズ中・ヒットストップ中は計算をスキップ
    if (scaledDt <= 0.0f) return;

    // 全体時間を進める
    m_globalTime += scaledDt;

    // ====================================================================
    // 2. 超高速な並列更新 (DOD最適化)
    // ====================================================================
    // ポインタ追跡（this->）を排除するため、レジスタにコピー
    const float t        = m_globalTime;
    const float orbitSpd = m_settings.orbitSpeed;
    const float waveSpd  = m_settings.waveSpeed;
    const float waveAmp  = m_settings.waveAmplitude;
    const float radScale = m_settings.radiusScale;

    ForEachParallel([t, orbitSpd, waveSpd, waveAmp, radScale](TransformComponent &trans, const OrbitBenchmarkComponent &orbit) {
        // 現在の角度 = 初期位相 + (全体時間 * 回転速度)
        float currentAngle = orbit.phaseOffset + (t * orbitSpd);

        // スケール適用後の半径
        float currentRadius = orbit.radius * radScale;

        // XZ平面の円軌道
        float x = std::cos(currentAngle) * currentRadius;
        float z = std::sin(currentAngle) * currentRadius;

        // Y軸の立体的な波打ち
        // 波の位相に元の半径(orbit.radius)を混ぜることで、内側から外側へ波紋のようにうねる
        float y = std::sin(currentAngle * waveSpd + orbit.radius) * waveAmp;

        trans.position = {x, y, z};

        // エンジンに行列再計算を通知
        trans.isStatic = false;
        trans.isDirty  = true;
    });
}

// -------------------------------------------------------------
// GUIでのリアルタイム編集
// -------------------------------------------------------------
void ECSBenchmarkSystem::OnGui()
{
    if (ImGui::CollapsingHeader("Galaxy Swarm Benchmark", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        ImGui::SliderFloat("Orbit Speed", &m_settings.orbitSpeed, -5.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Wave Speed", &m_settings.waveSpeed, 0.0f, 10.0f, "%.2f");
        ImGui::SliderFloat("Wave Amplitude", &m_settings.waveAmplitude, 0.0f, 50.0f, "%.1f");
        ImGui::SliderFloat("Radius Scale", &m_settings.radiusScale, 0.1f, 5.0f, "%.2f");

        ImGui::Separator();
        ImGui::Text("Global Time: %.2f", m_globalTime);
        ImGui::SameLine();
        if (ImGui::Button("Reset Time"))
        {
            m_globalTime = 0.0f;
        }

        ImGui::Unindent();
    }
}

REGISTER_LOGIC_SYSTEM(ECSBenchmarkSystem, Priority::LogicStage::L02_Update);