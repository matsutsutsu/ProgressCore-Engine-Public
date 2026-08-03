#include "PlayerLockOnSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Character/Boss/Data/BossActionComponent.h" // 敵を標的にする
#include "Game/Logics/Combat/DeadTag.h"         // 死亡確認用
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h" // ロックオン状態をカメラへ push する
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <SimpleMath.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerLockOnSystem::Update(float /*dt*/)
{
    ForEachWithID([&](EntityID playerID, PlayerComponent &player, const TransformComponent &playerTrans) {
        
        // ===================================================================
        // 1. ロックオン対象がすでに死亡、または世界から消滅している場合は自動解除（安全ハック）
        // ★修正: Exists ではなく IsEntityValid を使用する
        // ===================================================================
        if (player.lockOnTargetID != InvalidEntityID)
        {
            if (!m_world->IsEntityValid(player.lockOnTargetID) ||
                m_world->HasComponent<Tag::DeadTag>(player.lockOnTargetID))
            {
                player.lockOnTargetID = InvalidEntityID;
            }
        }

        // 2. Rボタンの要求があった時だけトグル処理を行う
        if (player.lockOnToggleRequested)
        {
            // トリガーフラグを消費（毎フレームの暴発防止）
            player.lockOnToggleRequested = false;

            // 3. すでにロックオン中なら、ボタン押下で「ロックオン解除」とする（トグル挙動）
            if (player.lockOnTargetID != InvalidEntityID)
            {
                player.lockOnTargetID = InvalidEntityID;
                CCL_LOG_INFO(LogCategory::Game, "Lock-On Cleared by User.");
            }
            else
            {
                // ===========================================================
                // 4. 【空間クエリ】世界中のドローンから「最もプレイヤーに近い個体」を探し出す
                // m_world->ForEachWithID ではなく、View() を使って対象IDを走査する
                // ===========================================================
                EntityID closestEnemyID = InvalidEntityID;
                float    closestDistSq  = 90.0f * 90.0f; // ロックオン可能な限界距離（例: 30メートル）の2乗

                // 世界中のドローンのEntityIDを一覧取得する
                auto droneView = m_world->View<BossActionComponent, TransformComponent>();

                for (EntityID enemyID : droneView)
                {
                    // 死亡している敵はスキップ
                    if (m_world->HasComponent<Tag::DeadTag>(enemyID)) continue;

                    // Transformを取得 (Viewで保証されているため即座にアクセス可能)
                    auto* enemyTrans = m_world->GetComponent<TransformComponent>(enemyID);
                    if (!enemyTrans) continue;

                    // 平面での純粋な距離計算
                    float distSq = Vector3::DistanceSquared(playerTrans.position, enemyTrans->position);
                    if (distSq < closestDistSq)
                    {
                        closestDistSq  = distSq;
                        closestEnemyID = enemyID;
                    }
                }

                // 5. ターゲットが確定したらプレイヤーの状態に書き込む
                if (closestEnemyID != InvalidEntityID)
                {
                    player.lockOnTargetID = closestEnemyID;
                    CCL_LOG_SUCCESS(LogCategory::Game, "Lock-On Target Confirmed! Target EntityID: %llu", closestEnemyID);
                }
            }
        }

        // ===================================================================
        // 6. 確定したロックオン状態をカメラへ push する（必ず最後に、毎フレーム）。
        //   カメラ(Engine)がプレイヤー(Game)を読みに来る形だと Engine→Game 依存に
        //   なるため、状態の持ち主であるこちらから書き込む。解除条件もここで完結する。
        // ===================================================================
        for (EntityID camID : m_world->View<CameraBodyTPSComponent>())
        {
            auto* tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
            if (!tps || tps->target.id != playerID) continue; // このプレイヤーを映していないカメラは対象外

            tps->lockOnTarget.id = player.lockOnTargetID;
            tps->isLockOnActive  = (player.lockOnTargetID != InvalidEntityID);
        }
    });
}

// 入力の直後、カメラや移動が動く前にターゲットを確定させる
REGISTER_LOGIC_SYSTEM(PlayerLockOnSystem, Priority::LogicStage::L02_Update);