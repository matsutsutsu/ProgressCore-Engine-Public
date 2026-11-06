#include "PlayerActionVfxSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/Systems/Particle/VFXSystem.h"
#include "Game/Core/GameEvents.h"

#include "Engine/Graphics/Components/Trail/TrailComponent.h"

using namespace CCL::ECS;

namespace
{
    // バーストエミッタのプレハブ（拡張子 .json は Prefab 側が付ける）。見た目はエディタで調整する
    const char *kGroundJumpVfxPrefab = "Assets/Prefabs/Particles/VFX_GroundJump";
    const char *kJumpVfxPrefab       = "Assets/Prefabs/Particles/VFX_Jump";
    const char *kDashVfxPrefab       = "Assets/Prefabs/Particles/VFX_Dash";
    const char *kJustEvadeVfxPrefab  = "Assets/Prefabs/Particles/VFX_JustEvade";

    // ダッシュとジャスト回避の VFX を出す高さ。足元から上へずらす（m）
    constexpr float kDashVfxYOffset = 2.0f;
} // namespace

void PlayerActionVfxSystem::Initialize()
{
    // 跳んだ瞬間に、その場に VFX を出す
    // 地面からの1段目と、空中の2段目で見た目を分ける
    ListenEvent<PlayerJumpedEvent>([this](const PlayerJumpedEvent &e) {
        const char *prefab = (e.jumpCount >= 2) ? kJumpVfxPrefab : kGroundJumpVfxPrefab;
        VFX::Play(*m_world, prefab, e.position);
    });

    // ダッシュ（回避）開始の瞬間に、その場に VFX を出し、足元のトレイルを出す
    // ジャスト回避が成立したら差し替えるので、出した VFX を覚えておく
    ListenEvent<PlayerDashedEvent>([this](const PlayerDashedEvent &e) {
        const DirectX::XMFLOAT3 vfxPosition = {e.position.x, e.position.y + kDashVfxYOffset, e.position.z};
        m_dashVfxID = VFX::Play(*m_world, kDashVfxPrefab, vfxPosition);

        ActivatePlayerFootTrails(e.playerID);
    });

    // ジャスト回避が成立したら、ダッシュの VFX を消して成立用の VFX に差し替える
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &e) {
        ReplaceDashVfxWithJustEvade(e.playerID);
    });
}



// ダッシュの VFX を消し、成立した瞬間のプレイヤーの位置に成立用の VFX を出す
void PlayerActionVfxSystem::ReplaceDashVfxWithJustEvade(CCL::ECS::EntityID playerID)
{
    // 粒子が途中で切れても、ダッシュと成立の VFX が重ならない方を取る
    // 寿命で消えた後なら、古い ID なので何もしない
    VFX::Destroy(*m_world, m_dashVfxID);
    m_dashVfxID = InvalidEntityID;

    auto *playerTransform = m_world->GetComponent<TransformComponent>(playerID);
    if (!playerTransform) return;

    DirectX::XMFLOAT3 vfxPosition = playerTransform->GetWorldPosition();
    vfxPosition.y += kDashVfxYOffset;
    VFX::Play(*m_world, kJustEvadeVfxPrefab, vfxPosition);
}



void PlayerActionVfxSystem::ActivatePlayerFootTrails(CCL::ECS::EntityID playerID)
{
    auto *playerTransform = m_world->GetComponent<TransformComponent>(playerID);
    if (!playerTransform) return;

    // 直接の子を兄弟リンク（firstChildID → nextSiblingID）でたどる。終端は 0（InvalidEntityID ではない）
    EntityID childID = playerTransform->firstChildID;
    while (childID != 0)
    {
        if (auto *trail = m_world->GetComponent<TrailComponent>(childID))
        {
            trail->ActivateFor(trail->burstDuration);
        }

        auto *childTransform = m_world->GetComponent<TransformComponent>(childID);
        if (!childTransform) break;

        childID = childTransform->nextSiblingID;
    }
}



// 処理はイベントで行う。HitEffectSystem と同じ L07_Cleanup に置く
REGISTER_LOGIC_SYSTEM(PlayerActionVfxSystem, Priority::LogicStage::L07_Cleanup);
