#include "PlayerMoveSystem.h"
#include "Engine/GamePlay/Graphics/Lighting/FogComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Engine/Graphics/Resource/Model.h"
#include <SimpleMath.h>
#include <cmath>

#include "Game/Logics/GameEvent/GameStateMode.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// ★本来はResourceManagerから取得しますが、コンパイルを通すための暫定的なキャッシュ
static AnimSequence s_tempSeqIdle;
static AnimSequence s_tempSeqRun;
static AnimSequence s_tempSeqAttack;

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

PlayerMoveSystem::PlayerMoveSystem() : IfSystem("PlayerMoveSystem") {}

void PlayerMoveSystem::Update(float dt)
{
    // 暫定的なアニメーションデータの初期化（※本来はJSON等からロードします）
    if (!_isAnimLoaded) {
        s_tempSeqIdle.targetAnimName = "Idle";   s_tempSeqIdle.duration = 2.0f;
        s_tempSeqRun.targetAnimName = "Run";     s_tempSeqRun.duration = 1.0f;
        s_tempSeqAttack.targetAnimName = "Attack"; s_tempSeqAttack.duration = 1.5f;

        _seqIdle = &s_tempSeqIdle;
        _seqRun = &s_tempSeqRun;
        _seqAttack = &s_tempSeqAttack;
        _isAnimLoaded = true;
    }

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


        // =================================================================
        //  移動と回転の計算を行う
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

            // --- B. 回転処理  ---
            // 回転はJoltに任せるのではなく、見た目のTransformを直接操作して回す方式にする
            // カプセルは常に直立不動なので、物理判定的には回転させない
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

        // --- 3. アニメーション制御 ---
        bool isMoving = (players.input.moveDir.x != 0.0f || players.input.moveDir.z != 0.0f);

        // ★修正: ポインタ比較で現在の状態を判定
        bool isAttacking = (animator.currentSequence == _seqAttack);

        if (isAttacking && !animator.isFinished) {
            // 攻撃モーションが終わるまで何もしない
        }
        else {
            // 通常の移動・待機モーション遷移
            const AnimSequence* nextSeq = _seqIdle;

            if (isMoving && _seqRun != nullptr) {
                nextSeq = _seqRun;
            }

            // ★ここで攻撃ボタンなどの入力があれば nextSeq = _seqAttack; にする等の処理が入ります
            // if (players.input.attack) { nextSeq = _seqAttack; }

            // ★追加した Play ヘルパー関数を呼び出す
            animator.Play(nextSeq, (nextSeq != _seqAttack), 1.0f); // 攻撃以外はループ再生
        }
    });
}


REGISTER_LOGIC_SYSTEM(PlayerMoveSystem, Priority::LogicStage::Update);