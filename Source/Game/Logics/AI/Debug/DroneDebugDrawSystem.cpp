#include "DroneDebugDrawSystem.h"

#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <DirectXMath.h>
#include <algorithm>

#include <imgui.h>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
    // 状態ごとの色と名前。添字は DroneState の値
    const XMFLOAT4 kStateColors[4] = {
        {0.3f, 1.0f, 0.3f, 1.0f},   // Idle         … 緑
        {1.0f, 0.9f, 0.2f, 1.0f},   // MoveToTarget … 黄
        {1.0f, 0.25f, 0.25f, 1.0f}, // FireCharge   … 赤
        {0.3f, 0.6f, 1.0f, 1.0f},   // Returning    … 青
    };
    const char *const kStateNames[4] = {"待機", "接近", "突撃", "離脱"};

    const XMFLOAT4 kUnlockedColor(0.8f, 0.3f, 1.0f, 1.0f); // 剛体になれていない機 … 紫

    constexpr float kOffSlotRadius = 1.5f; // 離れている機を囲む球の半径 [m]。線より面積を取り、遠くからでも見える大きさ
    constexpr float kSlotRadius    = 0.3f; // 持ち場の球の半径 [m]

    // 剛体追従の突撃中で、持ち場へバネで追いかけている最中か
    bool IsChasingRigidSlot(const DroneComponent &drone)
    {
        return drone.currentState == DroneState::FireCharge && drone.chargeMode == ChargeMode::RigidFollow &&
               !drone.rigidLocked && drone.attackProgress < 1.0f;
    }



    ImVec4 ToImVec4(const XMFLOAT4 &c) { return ImVec4(c.x, c.y, c.z, c.w); }
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void DroneDebugDrawSystem::Update(float dt)
{
    if (!m_enabled) return;

    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    m_stats = Stats{};

    // 全機で m_stats に足し込むので並列にしない（ShapeRenderer は並列で積めるが、1000機なら直列で十分軽い）
    ForEach([&](const TransformComponent &trans, const DroneComponent &drone) {
        const size_t state = static_cast<size_t>(drone.currentState);
        if (state >= 4) return;

        ++m_stats.byState[state];

        const bool unlocked = IsChasingRigidSlot(drone);
        if (unlocked) ++m_stats.unlocked;

        // 離脱は持ち場を離れるのが正しいので、数えるだけで描かない
        if (drone.currentState == DroneState::Returning) return;

        const Vector3 position(trans.position);
        const float   distance = Vector3::Distance(position, drone.targetPosition);
        m_stats.maxDistance    = (std::max)(m_stats.maxDistance, distance);

        if (distance <= m_minDistance) return;

        DrawOffSlot(renderer, position, drone.targetPosition, unlocked ? kUnlockedColor : kStateColors[state]);
    });
}



// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

void DroneDebugDrawSystem::DrawOffSlot(ShapeRenderer *renderer, const Vector3 &position, const Vector3 &slot,
                                       const XMFLOAT4 &color) const
{
    renderer->DrawSphere(position, kOffSlotRadius, color);
    renderer->DrawSphere(slot, kSlotRadius, color);
    renderer->DrawLine(position, slot, color);
}



// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void DroneDebugDrawSystem::OnGui()
{
    ImGui::Checkbox("持ち場とのずれを描く", &m_enabled);
    ImGui::SliderFloat("描くずれの下限", &m_minDistance, 0.0f, 10.0f, "%.1f m");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("持ち場からこれより離れている機だけ、球で囲んで持ち場とつなぐ");

    if (!m_enabled)
    {
        ImGui::TextDisabled("描いている間だけ数える");
        return;
    }

    // 色の凡例を兼ねる
    for (size_t i = 0; i < 4; ++i)
        ImGui::TextColored(ToImVec4(kStateColors[i]), "%s: %d機", kStateNames[i], m_stats.byState[i]);
    ImGui::SameLine();
    ImGui::TextDisabled("（離脱は数えるだけで描かない）");

    ImGui::TextColored(ToImVec4(kUnlockedColor), "剛体になれていない: %d機", m_stats.unlocked);
    ImGui::Text("最大のずれ（離脱中は除く）: %.1f m", m_stats.maxDistance);
}



// R08_Main で ShapeRenderer に積む
REGISTER_DEBUG_RENDER_SYSTEM(DroneDebugDrawSystem, Priority::RenderStage::R08_Main);
