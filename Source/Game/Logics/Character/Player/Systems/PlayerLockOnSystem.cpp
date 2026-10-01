#include "PlayerLockOnSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/EntityLabel.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Game/Logics/Combat/EnemyTag.h"                           // ロックオン候補の絞り込み
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h" // ロックオン状態をカメラへ書く
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <SimpleMath.h>
#include <limits>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerLockOnSystem::Update(float /*dt*/)
{
    ForEachWithID([&](EntityID playerID, PlayerComponent &player,
                      const PlayerInputComponent &pin, const TransformComponent &playerTrans) {
        // 1. ロックオン対象が死亡しているか破棄されていれば解除する。
        //    世代の違う ID を持ち続けないよう、Exists ではなく IsEntityValid で判定する。
        if (player.lockOnTargetID != InvalidEntityID)
        {
            if (!m_world->IsEntityValid(player.lockOnTargetID) ||
                m_world->HasComponent<Tag::DeadTag>(player.lockOnTargetID))
            {
                player.lockOnTargetID = InvalidEntityID;
            }
        }

        // 2. ロックオンのボタンを押した瞬間だけ切り替える。
        //    押した瞬間の判定は PlayerInputSystem が済ませているので、ここでフラグを false にしない。
        //    false にしても入力側が同じフレームの次のステップで書き直すため、
        //    1回の押下で切り替えが2回走って元に戻ることがある。
        if (pin.lockOnPressed)
        {
            // 3. ロックオン中なら解除する
            if (player.lockOnTargetID != InvalidEntityID)
            {
                player.lockOnTargetID = InvalidEntityID;
                CCL_LOG_INFO(LogCategory::Game, "Lock-On Cleared by User.");
            }
            else
            {
                // 4. EnemyTag を持つ生存中の敵から、プレイヤーに最も近いものを探す。
                //    候補を一覧に溜めず、走査しながら最短を更新する。
                //    候補は EnemyTag だけで絞る。特定の Component（BossActionComponent など）で絞ると、
                //    敵の種類が増えるたびにここへ条件を足すことになる。
                //
                //    距離の上限は設けない。ステージが広く、ボスは跳躍や滞空で一気に間合いを
                //    離すため、上限を置くと「離れた瞬間だけロックオンを受け付けない」という
                //    操作感になる。遠い敵が小さく映る問題はカメラ側の引き
                //    （lockOnDistanceGain）が吸収するので、ここで弾く必要がない。
                EntityID closestEnemyID = InvalidEntityID;
                float    closestDistSq  = (std::numeric_limits<float>::max)();

                m_world->ViewEach<Tag::EnemyTag, TransformComponent>([&](EntityID enemyID) {
                    if (m_world->HasComponent<Tag::DeadTag>(enemyID)) return;

                    auto *enemyTrans = m_world->GetComponent<TransformComponent>(enemyID);
                    if (!enemyTrans) return;

                    // 高さも含めた3D の距離で比べる
                    float distSq = Vector3::DistanceSquared(playerTrans.position, enemyTrans->position);
                    if (distSq < closestDistSq)
                    {
                        closestDistSq  = distSq;
                        closestEnemyID = enemyID;
                    }
                });

                // 5. 見つかったらロックオン対象にする
                if (closestEnemyID != InvalidEntityID)
                {
                    player.lockOnTargetID = closestEnemyID;
                    CCL_LOG_SUCCESS(LogCategory::Game, "Lock-On Target Confirmed! Target: %s",
                                    CCL::EntityLabel(*m_world, closestEnemyID).c_str());
                }
            }
        }

        // 6. 決まったロックオン状態を、毎フレーム最後にカメラへ書く。
        //   カメラ(Engine)がプレイヤー(Game)を読む形にすると Engine→Game の依存になるため、
        //   状態を持つこちらから書き込む。
        m_world->ViewEach<CameraBodyTPSComponent>([&](EntityID camID) {
            auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
            if (!tps || tps->target.id != playerID) return; // このプレイヤーを映していないカメラは飛ばす

            tps->lockOnTarget.id = player.lockOnTargetID;
            tps->isLockOnActive  = (player.lockOnTargetID != InvalidEntityID);
        });
    });
}

// PlayerInputSystem(L01_Input) の後に走り、カメラ（Render ステージ）より前に対象を決める
REGISTER_LOGIC_SYSTEM(PlayerLockOnSystem, Priority::LogicStage::L02_Update);