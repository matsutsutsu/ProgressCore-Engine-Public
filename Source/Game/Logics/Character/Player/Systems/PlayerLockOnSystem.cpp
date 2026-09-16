#include "PlayerLockOnSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/EntityLabel.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h" // 敵を標的にする
#include "Game/Logics/Combat/DeadTag.h"                                // 死亡確認用
#include "Game/Logics/Combat/EnemyTag.h"                               // ロックオン候補の絞り込み
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"     // ロックオン状態をカメラへ push する
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <SimpleMath.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerLockOnSystem::Update(float /*dt*/)
{
    ForEachWithID([&](EntityID playerID, PlayerComponent &player,
                      const PlayerInputComponent &pin, const TransformComponent &playerTrans) {
        // ===================================================================
        // 1. ロックオン対象が死亡済み、または破棄済みなら自動解除する。
        //    世代違いのIDを掴んだままにしないため Exists ではなく IsEntityValid で判定する。
        // ===================================================================
        if (player.lockOnTargetID != InvalidEntityID)
        {
            if (!m_world->IsEntityValid(player.lockOnTargetID) ||
                m_world->HasComponent<Tag::DeadTag>(player.lockOnTargetID))
            {
                player.lockOnTargetID = InvalidEntityID;
            }
        }

        // 2. Rボタンを押した瞬間だけトグル処理を行う。
        //    押下の立ち上がり判定は PlayerInputSystem が済ませているので、ここでフラグを
        //    消費してはいけない。消費しても入力側が同じフレームの次ステップで書き直すため、
        //    1回の押下でトグルが2回走って元に戻る（＝Rが効かない）ことがあった。
        if (pin.lockOnPressed)
        {
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

                // 候補を一覧に貯めず、その場で最短を更新していく
                // ロックオン候補は EnemyTag を持つもの。
                //   特定のコンポーネント（BossActionComponent 等）で絞らないこと。
                //   敵の種類が増えるたびにここへ条件を足すはめになる。
                m_world->ViewEach<Tag::EnemyTag, TransformComponent>([&](EntityID enemyID) {
                    // 死亡している敵はスキップ
                    if (m_world->HasComponent<Tag::DeadTag>(enemyID)) return;

                    auto *enemyTrans = m_world->GetComponent<TransformComponent>(enemyID);
                    if (!enemyTrans) return;

                    // 平面での純粋な距離計算
                    float distSq = Vector3::DistanceSquared(playerTrans.position, enemyTrans->position);
                    if (distSq < closestDistSq)
                    {
                        closestDistSq  = distSq;
                        closestEnemyID = enemyID;
                    }
                });

                // 5. ターゲットが確定したらプレイヤーの状態に書き込む
                if (closestEnemyID != InvalidEntityID)
                {
                    player.lockOnTargetID = closestEnemyID;
                    CCL_LOG_SUCCESS(LogCategory::Game, "Lock-On Target Confirmed! Target: %s",
                                    CCL::EntityLabel(*m_world, closestEnemyID).c_str());
                }
            }
        }

        // ===================================================================
        // 6. 確定したロックオン状態をカメラへ push する（必ず最後に、毎フレーム）。
        //   カメラ(Engine)がプレイヤー(Game)を読みに来る形だと Engine→Game 依存に
        //   なるため、状態の持ち主であるこちらから書き込む。解除条件もここで完結する。
        // ===================================================================
        m_world->ViewEach<CameraBodyTPSComponent>([&](EntityID camID) {
            auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
            if (!tps || tps->target.id != playerID) return; // このプレイヤーを映していないカメラは対象外

            tps->lockOnTarget.id = player.lockOnTargetID;
            tps->isLockOnActive  = (player.lockOnTargetID != InvalidEntityID);
        });
    });
}

// 入力の直後、カメラや移動が動く前にターゲットを確定させる
REGISTER_LOGIC_SYSTEM(PlayerLockOnSystem, Priority::LogicStage::L02_Update);