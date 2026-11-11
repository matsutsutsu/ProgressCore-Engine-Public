#include "BattleStartFacingSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"

#include <cmath>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

void BattleStartFacingSystem::Update(float /*dt*/)
{
    if (m_applied) return;
    m_applied = TryApplyFacing();
}



bool BattleStartFacingSystem::TryApplyFacing()
{
    if (!m_world->HasResource<BattleContext>()) return false;

    const auto &battle = m_world->GetResource<BattleContext>();

    // プレハブ生成は遅延されるため、開始直後の数フレームは揃っていない
    // ボスがいないシーンでは永遠に揃わないが、その場合は既定の向き
    // （プレイヤー=+Z / カメラ=プレハブ値）のままで支障がない
    if (!battle.HasPlayer() || !battle.HasBoss()) return false;

    Vector3 toBoss = Vector3(battle.bossPos) - Vector3(battle.playerPos);
    toBoss.y       = 0.0f; // 水平面だけで向きを決める（見上げ角はピッチが持つ）

    // ほぼ真上に重なっている異常配置。向きを決められないので次フレームへ回す
    if (toBoss.LengthSquared() < 1e-4f) return false;
    toBoss.Normalize();

    // --- プレイヤーをボスへ向ける ---
    // PlayerJumpFacingSystem と同じ規約: atan2(x, z) でラジアン、0 が +Z
    if (auto *player = m_world->GetComponent<PlayerComponent>(battle.playerEntity))
    {
        player->currentYaw  = std::atan2(toBoss.x, toBoss.z);
        player->currentRoll = 0.0f; // 保存されたバンク角が残ると開始時に傾いて見える
    }

    // --- 追従カメラをプレイヤーの背後へ回す ---
    // CameraActionTPSSystem と同じ規約: atan2(z, x) で度、0 が +X
    // カメラ位置は pivot - backwardDir * distance なので、backwardDir を
    // 「プレイヤー→ボス」に合わせると、カメラはプレイヤーの背後に立ち、
    // 視線がプレイヤー越しにボスを向く
    const float camYawDeg = XMConvertToDegrees(std::atan2(toBoss.z, toBoss.x));

    int oriented = 0;
    ForEach([&](CameraBodyTPSComponent &tps) {
        // プレイヤーを追っているカメラだけが対象
        if (tps.target.id != battle.playerEntity) return;

        tps.currentYaw = camYawDeg;

        // 補間を飛ばして一度で合わせる。これがないと、元の向きから positionDamping で
        // 補間されて、開始直後にカメラが勝手に回って見える
        tps.isFirstFrame = true;
        ++oriented;
    });

    CCL_LOG_INFO(LogCategory::Game,
                 "[BattleStartFacing] 開始向きを確定 (プレイヤーyaw=%.1f deg / カメラyaw=%.1f deg / カメラ%d台)",
                 XMConvertToDegrees(std::atan2(toBoss.x, toBoss.z)), camYawDeg, oriented);

    return true;
}



// BattleContextSyncSystem(L01_WorldState) の後、PlayerJumpFacingSystem より前（同じ L02_PreUpdate なので GetRunBefore で指定）
REGISTER_LOGIC_SYSTEM(BattleStartFacingSystem, Priority::LogicStage::L02_PreUpdate);
