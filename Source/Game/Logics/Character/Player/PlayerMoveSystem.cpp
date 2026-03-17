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

    ForEachWithID([&](EntityID            id,
                      TransformComponent &trans,
                      const JoltCharacterHandleComponent &charHandle,
                      const JoltCharacterConfigComponent &charConfig,
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
        // タイトル画面でない場合のみ、移動と回転の計算を行う
        // =================================================================
        if (charHandle.character) {
            // --- A. 移動処理 ---
            Vector3 moveVec(players.input.moveDir.x, 0, players.input.moveDir.z);
            
            // Joltの速度ベクトルを用意（現在のY軸の落下速度を維持する）
            JPH::Vec3 currentVel = charHandle.character->GetLinearVelocity();
            JPH::Vec3 joltVelocity(0.0f, currentVel.GetY(), 0.0f);

            if (moveVec.LengthSquared() > 0.001f) {
                moveVec.Normalize();
                // configに設定された歩行速度を適用
                joltVelocity.SetX(moveVec.x * charConfig.walkSpeed);
                joltVelocity.SetZ(moveVec.z * charConfig.walkSpeed);
            }

            // --- ジャンプ処理 ---
            // IsSupported() で足元に地面（SupportingVolume）があるか判定
            if (players.input.jump && charHandle.character->IsSupported()) {
                joltVelocity.SetY(charConfig.jumpSpeed); 
            }

            // 速度をキャラクターコントローラーに適用！
            charHandle.character->SetLinearVelocity(joltVelocity);

            // --- B. 回転処理 (以前のまま) ---
            Vector3 targetPos  = players.input.lookPos;
            Vector3 currentPos = trans.GetWorldPosition();
            Vector3 lookDir = targetPos - currentPos;
            lookDir.y       = 0.0f;

            if (lookDir.LengthSquared() > 0.001f) {
                lookDir.Normalize();
                float      targetYaw = atan2f(lookDir.x, lookDir.z);
                Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
                trans.rotation = Quaternion::Slerp(trans.rotation, targetRot, players.turnSpeed * dt);
            }
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
    });
}


REGISTER_LOGIC_SYSTEM(PlayerMoveSystem, Priority::LogicStage::Update);