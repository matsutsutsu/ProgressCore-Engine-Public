#include "VoxelOceanSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

// 必要なインクルードを追加
#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif
#include "Engine/Timer/TimeState.h"

void VoxelOceanSystem::Update(float dt)
{
    if (!_world || !_world->HasResource<TimeContext>()) return;
    auto &ctx = _world->GetResource<TimeContext>();

    // ------------------------------------------------------------
    // 1. タイムスケールの適用 (Environmentグループの時間を取得)
    // -------------------------------------------------------------
    float timeScale = ctx.globalScale;

    // タイムスケールを適用した経過時間を進める
    m_globalTime += dt * timeScale * m_settings.waveSpeed;

    // -------------------------------------------------------------
    // 2. 数万個のVoxelの更新 (DOD最適化)
    // -------------------------------------------------------------
    // ローカル変数にコピーしておくことで、ラムダ式内のポインタ間接参照(this->)を排除し高速化する
    const float t      = m_globalTime;
    const float amp    = m_settings.waveAmplitude;
    const float freq   = m_settings.waveFrequency;
    const float offset = m_settings.heightOffset;

    ForEachParallel([t, amp, freq, offset](TransformComponent &trans, const VoxelOceanComponent &ocean) {
        float x = trans.position.x * freq;
        float z = trans.position.z * freq;

        // 複合波の合成（Gerstner Wave 簡易版）
        float wave1 = std::sin(x * 0.1f + t) * 2.0f;
        float wave2 = std::cos(z * 0.15f + t * 1.2f) * 1.5f;
        float wave3 = std::sin((x + z) * 0.2f - t * 1.5f) * 1.0f;

        // 振幅と基本オフセットを適用
        trans.position.y = offset + ((wave1 + wave2 + wave3) * amp);

        // 描画エンジンに行列の再計算を要求する
        trans.isDirty  = true;
        trans.isStatic = false;
    });
}

// -------------------------------------------------------------
// GUIでのリアルタイム編集
// -------------------------------------------------------------
#ifdef ENABLE_EDITOR
void VoxelOceanSystem::OnGui()
{
    if (ImGui::CollapsingHeader("Voxel Ocean Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        ImGui::SliderFloat("Wave Speed", &m_settings.waveSpeed, 0.0f, 10.0f, "%.2f");
        ImGui::SliderFloat("Wave Amplitude", &m_settings.waveAmplitude, 0.0f, 10.0f, "%.2f");
        ImGui::SliderFloat("Wave Frequency", &m_settings.waveFrequency, 0.1f, 5.0f, "%.2f");
        ImGui::DragFloat("Height Offset", &m_settings.heightOffset, 0.1f, -50.0f, 50.0f, "%.2f");

        ImGui::Separator();
        ImGui::Text("Global Ocean Time: %.2f", m_globalTime);

        ImGui::Unindent();
    }
}
#else
void VoxelOceanSystem::OnGui() {}
#endif


// LogicグループのUpdateフェーズで回す
REGISTER_LOGIC_SYSTEM(VoxelOceanSystem, Priority::LogicStage::L02_Update)