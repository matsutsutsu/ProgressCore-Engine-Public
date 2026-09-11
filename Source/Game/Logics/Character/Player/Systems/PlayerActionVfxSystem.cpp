// 役割・設計方針は PlayerActionVfxSystem.h を参照。
#include "PlayerActionVfxSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/Systems/Particle/VFXSystem.h"
#include "Game/Logics/GameEvent/GameEvents.h"

#include "Engine/Graphics/Components/Trail/TrailComponent.h"

using namespace CCL::ECS;

namespace
{
// バーストエミッタ・プレハブ（.json は Prefab 側が補完する）。見た目はエディタで調整する。
const char *kJumpVfxPrefab = "Assets/Prefabs/Particles/VFX_Jump";
const char *kDashVfxPrefab = "Assets/Prefabs/Particles/VFX_Dash";
} // namespace

void PlayerActionVfxSystem::Initialize()
{
    // 2段ジャンプ（jumpCount>=2）の瞬間だけ、その場に土煙VFXを出す。
    //   1段目（地上ジャンプ）では出さない＝空中で「もう一度蹴った」感触を強調する。
    ListenEvent<PlayerJumpedEvent>([this](const PlayerJumpedEvent &e) {
        if (e.jumpCount >= 2)
            VFX::Play(*m_world, kJumpVfxPrefab, e.position);
    });

    // ダッシュ（回避）開始の瞬間に、その場にVFXを出す。
    ListenEvent<PlayerDashedEvent>([this](const PlayerDashedEvent &e) {
        float             vfxYOffset  = 2.0f;
        DirectX::XMFLOAT3 vfxPosition = {e.position.x, e.position.y + vfxYOffset, e.position.z};

        VFX::Play(*m_world, kDashVfxPrefab, vfxPosition);

        // プレイヤーの足元のトレイルを表示する
        ActivePlayerFootTrails(e.playerID);
    });
}

void PlayerActionVfxSystem::Update(float /*dt*/)
{
    // 何もしない。再生中インスタンスの後始末は VFXSystem(L08) が持つ。
    //   購読のためだけに System として存在している。
}

void PlayerActionVfxSystem::ActivePlayerFootTrails(CCL::ECS::EntityID playerID)
{
    // 子供捜索のためにTransformComponentを取得
    auto *playerTransform = m_world->GetComponent<TransformComponent>(playerID);
    if (!playerTransform) return;

    // 最初の子供Entityを取得
    EntityID childID = playerTransform->firstChildID;

    // 子供EntityにTrailComponentがあるか調べて
    // そこから双方向リンクリストなので次の子供IDをセットしてループする
    while (childID != InvalidEntityID)
    {
        // このEntity自身にTrailがあればON
        if (auto *trail = m_world->GetComponent<TrailComponent>(childID))
        {
            trail->ActivateFor(trail->burstDuration);
        }

        auto *childTransform = m_world->GetComponent<TransformComponent>(childID);

        // 子供操作し終わったら処理を抜ける
        if (!childTransform) break;

        // 同じ子供階層の次の子供にchildIDをセットしてループ
        childID = childTransform->nextSiblingID;
    }
}

// 実処理はイベント駆動。片付け相当のタイミングに置く（HitEffectSystem と同じ L07）。
REGISTER_LOGIC_SYSTEM(PlayerActionVfxSystem, Priority::LogicStage::L07_Cleanup);
