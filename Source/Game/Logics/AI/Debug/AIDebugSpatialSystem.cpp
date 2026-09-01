// AIDebugSpatialSystem の実装。ShapeRenderer で間合いリングと照準線を描く。
#include "AIDebugSpatialSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <DirectXMath.h>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::ECS;
using namespace DirectX;

void AIDebugSpatialSystem::Update(float /*dt*/)
{
#ifdef ENABLE_EDITOR
    if (!m_enabled) return;

    // ShapeRenderer が無ければ描けない（NavMesh 等と同じ前提）。
    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    // プレイヤー座標は BattleContext（世界の事実）から。無ければ照準線だけ省く。
    const bool hasBattle = m_world->HasResource<BattleContext>();
    XMFLOAT3   playerPos(0, 0, 0);
    bool       hasPlayer = false;
    if (hasBattle)
    {
        const auto &battle = m_world->GetResource<BattleContext>();
        playerPos = XMFLOAT3(battle.playerPos.x, battle.playerPos.y, battle.playerPos.z);
        hasPlayer = battle.HasPlayer();
    }

    // XY平面で定義される円を X軸+90° 回転させて地面(XZ平面)へ寝かせる。
    const XMFLOAT4 groundQuat(0.70710678f, 0.0f, 0.0f, 0.70710678f);
    const XMFLOAT4 colNear(1.0f, 0.35f, 0.20f, 1.0f); // 近接(<8m)：赤オレンジ
    const XMFLOAT4 colFar(0.20f, 0.70f, 1.0f, 1.0f);  // 接近(>40m)：シアン
    const XMFLOAT4 colLine(1.0f, 0.90f, 0.20f, 1.0f); // 照準線：黄

    ForEach([&](const TransformComponent &trans, const BossActionComponent & /*boss*/) {
        // 地面にわずかに浮かせて Z ファイティングを避ける。
        const XMFLOAT3 center(trans.position.x, trans.position.y + 0.05f, trans.position.z);

        renderer->DrawWireframeCircle(center, m_ringNear, colNear, groundQuat);
        renderer->DrawWireframeCircle(center, m_ringFar, colFar, groundQuat);

        if (m_drawLine && hasPlayer)
        {
            const XMFLOAT3 pTarget(playerPos.x, playerPos.y + 0.05f, playerPos.z);
            renderer->DrawArrow(center, pTarget, 0.5f, colLine);
        }
    });
#endif 
}

#ifdef ENABLE_EDITOR
void AIDebugSpatialSystem::OnGui()
{
    // 別ウィンドウは作らず、System Manager の各システム欄（このヘッダ配下）に直接出す。
    ImGui::Checkbox("Show Range Rings", &m_enabled);
    ImGui::Checkbox("Show Lock-on Line", &m_drawLine);
    ImGui::SliderFloat("Ring Near (<m)", &m_ringNear, 1.0f, 30.0f);
    ImGui::SliderFloat("Ring Far  (>m)", &m_ringFar, 10.0f, 80.0f);
    ImGui::TextDisabled("BTの分岐しきい値(8m/40m)を地面に可視化");
}
#else
void AIDebugSpatialSystem::OnGui() {}
#endif

// NavMeshDebugDrawSystem と同じ R08_Main で ShapeRenderer に積む。
REGISTER_RENDER_SYSTEM(AIDebugSpatialSystem, Priority::RenderStage::R08_Main);
