#include "PlayerLockOnSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/EntityLabel.h"
#include "Game/Logics/Combat/Components/DeadTag.h"
#include "Game/Logics/Combat/Components/EnemyTag.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"
#include "Game/Logics/Camera/PlayerCameraComponent.h"
#include "Game/Logics/Camera/PlayerCameraLookup.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <SimpleMath.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

std::vector<CCL::ECS::TypeID> PlayerLockOnSystem::GetReadTypes() const
{
    auto types = Base::GetReadTypes();
    types.push_back(TypeInfo<PlayerRushComponent>::ID()); // ラッシュ中は、カメラにラッシュの相手を狙わせる
    return types;
}



std::vector<CCL::ECS::TypeID> PlayerLockOnSystem::GetWriteTypes() const
{
    auto types = Base::GetWriteTypes();
    types.push_back(TypeInfo<PlayerCameraComponent>::ID()); // カメラのロックオンの欄
    return types;
}



void PlayerLockOnSystem::Update(float dt)
{
    ForEachWithID([&](EntityID playerID, PlayerComponent &player,
                      const PlayerInputComponent &pin, const TransformComponent &playerTrans) {
            
        // 1. ロックオンの対象が削除されていたらロックオンを解除する
        // 世代の違う ID を持ち続けないよう、Exists ではなく IsEntityValid で判定する
        // ロックオンしていないときも false になり、無効のまま変わらない
        if (!m_world->IsEntityValid(player.lockOnTargetID) ||
            m_world->HasComponent<Tag::DeadTag>(player.lockOnTargetID))
        {
            player.lockOnTargetID = InvalidEntityID;
        }

        // 2. ロックオンのボタンを押した瞬間にロックオンする
        if (pin.lockOnPressed)
        {
            // 3. ロックオン中なら解除する
            if (player.lockOnTargetID != InvalidEntityID)
            {
                player.lockOnTargetID = InvalidEntityID;               
            }
            else
            {
                // 4. EnemyTag を持つ敵から、プレイヤーに近いものを探す
                EntityID closestEnemyID = InvalidEntityID;
                float    closestDistSq  = 0.0f;

                // 生きている敵全ての距離を比べて一番距離が短い敵を取得する
                m_world->ViewEach<Tag::EnemyTag, TransformComponent>([&](EntityID enemyID) {
                    // 死んでいる敵は除外する
                    if (m_world->HasComponent<Tag::DeadTag>(enemyID)) return;


                    auto *enemyTrans = m_world->GetComponent<TransformComponent>(enemyID);
                    if (!enemyTrans) return;

                    // 高さも含めた距離で比べる
                    float distSq = Vector3::DistanceSquared(playerTrans.position, enemyTrans->position);

                    // 1体目はそのまま採り、2体目からは近いほうに入れ替える
                    if (closestEnemyID == InvalidEntityID || distSq < closestDistSq)
                    {
                        closestDistSq  = distSq;

                        // 一番敵のIDをセットする
                        closestEnemyID = enemyID;
                    }
                });

                // 5. 見つかったらロックオン対象にする
                if (closestEnemyID != InvalidEntityID)
                {
                    player.lockOnTargetID = closestEnemyID;
                }
            }
        }

        // 6. 最終的なロックオン状態をプレイヤーのカメラコンポーネントに書き込む

        // 最後にラッシュ中ならロックオンした敵じゃなくて攻撃をよけた敵にターゲットを向ける
        const EntityID cameraTarget = PickCameraTarget(playerID, player);

        if (auto *tps = m_world->GetComponent<PlayerCameraComponent>(FindPlayerCamera(*m_world, playerID)))
        {
            tps->lockOnTarget.id = cameraTarget;
            tps->isLockOnActive  = (cameraTarget != InvalidEntityID);
        }
    });
}



// カメラに狙わせる敵を、ラッシュの相手かロックオンの相手から選ぶ
EntityID PlayerLockOnSystem::PickCameraTarget(EntityID playerID, const PlayerComponent &player) const
{
    // ラッシュの相手が死ぬと、PlayerRushSystem が無効にする
    if (const auto *rush = m_world->GetComponent<PlayerRushComponent>(playerID))
    {
        if (rush->isActive && rush->closeInTarget != InvalidEntityID) return rush->closeInTarget;
    }

    return player.lockOnTargetID; // EntityLink から暗黙に変換する。手順1で死亡・破棄は外してある
}



REGISTER_LOGIC_SYSTEM(PlayerLockOnSystem, Priority::LogicStage::L02_Update);
