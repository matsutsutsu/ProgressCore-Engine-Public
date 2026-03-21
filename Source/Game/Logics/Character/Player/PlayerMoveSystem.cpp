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
            // 1. 現在の物理速度を取得
            JPH::Vec3 currentVel = charHandle.character->GetLinearVelocity();

            // 2. 重力の適用 (毎秒 -9.8f の加速を dt 倍して加算)
            float newVelocityY = currentVel.GetY() - (9.8f * dt);

            // 3. 地面との接地判定 (IsSupported)
            // 足元に地面がある場合、無限に重力が蓄積しないようにリセットする。
            // ※完全に 0.0f にするより、微小なマイナス(-0.1f)を残しておくことで、
            // 　下り坂を歩く時に「ピタッ」と地面に張り付く（跳ねない）ようになります。
            if (charHandle.character->IsSupported()) {
                if (newVelocityY < 0.0f) {
                    newVelocityY = -0.1f; 
                }
            }

            // 新しい速度ベクトルを構築
            JPH::Vec3 joltVelocity(0.0f, newVelocityY, 0.0f);

            // 4. X軸・Z軸のプレイヤー入力（WASD）の処理
            Vector3 moveVec(players.input.moveDir.x, 0, players.input.moveDir.z);
            if (moveVec.LengthSquared() > 0.001f) {
                moveVec.Normalize();
                joltVelocity.SetX(moveVec.x * charConfig.walkSpeed);
                joltVelocity.SetZ(moveVec.z * charConfig.walkSpeed);
            }

            // 5. ジャンプ処理
            // ジャンプボタンが押され、かつ足が地面に着いている時だけY軸の速度を上書き！
            if (players.input.jump && charHandle.character->IsSupported()) {
                joltVelocity.SetY(charConfig.jumpSpeed);
            }

            // 6. 計算し終わった速度をCharacterVirtualに叩き込む！
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