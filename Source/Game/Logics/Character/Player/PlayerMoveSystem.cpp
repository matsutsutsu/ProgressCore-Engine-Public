#include "PlayerMoveSystem.h"

// 実装詳細でのみ必要なインクルード
#include "Engine/GamePlay/Graphics/Lighting/FogComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Engine/Graphics/Resource/Model.h"
#include <SimpleMath.h>
#include <cmath> // atan2f用

#include "Game/Logics/GameEvent/GameStateMode.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

PlayerMoveSystem::PlayerMoveSystem() : IfSystem("PlayerMoveSystem") {}

void PlayerMoveSystem::Update(float dt)
{
    // 1. フォグコンポーネントへのポインタを準備
    FogComponent *pFog = nullptr;

    // FogComponentを持つエンティティ一覧を取得
    auto fogs = _world->View<FogComponent>();
    if (!fogs.empty()) {
        // 最初に見つかった有効なフォグコンポーネントを取得しておく
        pFog = _world->GetComponent<FogComponent>(fogs[0]);
    }

    // ★追加: 現在がタイトル画面かどうかをチェック
    bool isTitle = false;
    if (_world->HasResource<GameState>()) {
        const auto &state = _world->GetResource<GameState>();
        isTitle           = (state.mode == GameStateMode::Title);
    }


    ForEachWithID([&](EntityID            id,
                      TransformComponent &trans,
                      RigidBodyComponent &rigid,
                      const PlayerComponent &players,
                      AnimatorComponent &animator,
                      const ModelComponent     &modelComp) {
        // そもそもHPコンポーネントを取得しなくても、「死んでいる(Tag::DeadTagがある)」かどうかが分かります
        if (_world->HasComponent<Tag::DeadTag>(id)) {
            return; // 死んでいたら何もさせない（アニメーションも更新しない）
        }

        // --- 0. アニメーションIDの取得 (初回のみ) ---
        if (modelComp.modelPtr) {
            if (_animIdIdle == -1) _animIdIdle = modelComp.modelPtr->GetAnimationIndex("Idle");
            // if (_animIdRun == -1) _animIdRun = modelComp.modelPtr->GetAnimationIndex("Run");
            if (_animIdAttack == -1)
                _animIdAttack = modelComp.modelPtr->GetAnimationIndex("Attack");
        }

        // =================================================================
        // ★修正: タイトル画面でない場合のみ、移動と回転の計算を行う
        // =================================================================
        if (!isTitle) {
            // --- A. 移動処理 ---
            Vector3 moveVec(players.input.moveDir.x, 0, players.input.moveDir.z);
            if (moveVec.LengthSquared() > 0.001f) {
                moveVec.Normalize();
                rigid.velocity.x = moveVec.x * players.moveSpeed;
                rigid.velocity.z = moveVec.z * players.moveSpeed;
            }
            else {
                rigid.velocity.x = 0.0f;
                rigid.velocity.z = 0.0f;
            }

            // --- B. 回転処理 (マウスの方を向く) ---
            Vector3 targetPos  = players.input.lookPos;
            Vector3 currentPos = trans.GetWorldPosition();

            Vector3 lookDir = targetPos - currentPos;
            lookDir.y       = 0.0f;

            if (lookDir.LengthSquared() > 0.001f) {
                lookDir.Normalize();
                float      targetYaw = atan2f(lookDir.x, lookDir.z);
                Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
                trans.rotation =
                    Quaternion::Slerp(trans.rotation, targetRot, players.turnSpeed * dt);
            }
        }
        else {
            // --- タイトル画面の場合 ---
            // 移動は強制停止 (念のため)
            rigid.velocity.x = 0.0f;
            rigid.velocity.z = 0.0f;

            // ★重要: 回転処理をスキップすることで、初期化時に設定した向きが維持されます。
        }

        // --- 3. アニメーション制御 ---

        bool isMoving = (players.input.moveDir.x != 0.0f || players.input.moveDir.z != 0.0f);

        // 現在アクティブなアニメーションIDを取得
        int currentActiveId =
            (animator.targetAnimIndex != -1) ? animator.targetAnimIndex : animator.currentAnimIndex;

        // ★優先度チェック:
        // もし「攻撃中」かつ「まだ終わっていない」なら、移動モーションによる上書きを拒否する
        bool isAttacking = (currentActiveId == _animIdAttack);

        if (isAttacking && !animator.isFinished) {
            // 何もしない（攻撃モーション継続）
        }
        else {
            // 通常の移動・待機モーション遷移
            int nextAnimId = _animIdIdle;
            if (isMoving && _animIdRun != -1) {
                nextAnimId = _animIdRun;
            }

            // 状態が変わる場合のみPlay
            if (nextAnimId != -1 && currentActiveId != nextAnimId) {
                animator.Play(nextAnimId, true, 1.0f, 0.2f);
            }
        }

        // ここでフォグの中心座標を更新
        if (pFog) {
            // プレイヤーの現在位置 (trans.position) を フォグの中心 (pFog->center) に適用
            // FogComponent::center は XMFLOAT4 なので、x, y, z をコピーし、w は 1.0f にする
            pFog->center = {trans.position.x, trans.position.y, trans.position.z, 1.0f};

            // もし「少し先」を照らしたい場合は、ターゲットカーソル位置(targetPos)や
            // 進行方向を加味した座標を入れるなどの調整も可能です
        }
    });
}


REGISTER_LOGIC_SYSTEM(PlayerMoveSystem, Priority::LogicStage::Update);