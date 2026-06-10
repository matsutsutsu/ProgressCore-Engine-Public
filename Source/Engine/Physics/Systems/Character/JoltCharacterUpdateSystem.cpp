#include "JoltCharacterUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include <Jolt/Physics/Body/BodyFilter.h>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

// ===================================================================================
// 【 物理肉体更新システム : JoltCharacterUpdateSystem 】
//
// [ 役割 ]
// 「誰が運転しているか」を一切気にせず、入力された速度でキャラクターを地形に沿わせて動かす「運び屋」。
//
// [ ECSデータパイプライン ]
// 📥 READ  : CharacterMovementInputComponent (希望する移動速度 / ジャンプ要求)
// 📥 READ  : JoltCharacterHandleComponent    (Jolt側のキャラクター実体)
// 📥 READ  : JoltCharacterConfigComponent     (jumpSpeed / maxStepHeight 等の性能値)
// 📤 WRITE : TransformComponent              (地形干渉を計算した後の最終ワールド座標)
//
// [ 重力に関する重要事項 ]
// JPH::CharacterVirtual::ExtendedUpdate は重力を速度へ積分しない。
// 第2引数 inGravity は階段昇降・床吸着の「下方向」を知るためだけに使われる。
// よって呼び出し側で gravity * dt を毎フレーム速度に加算する必要がある。
// ===================================================================================


/**
 * @brief CharacterVirtual に重力・接地・ジャンプを正しく積分して移動させる。
 * @param dt フレーム時間（秒）。
 * @details
 *  - 重力は ExtendedUpdate が積分しないため、本関数で gravity*dt を加算する。
 *  - 接地判定は GetGroundState() を唯一の真実とする（レイキャストは使わない）。
 *  - 接地中の下向き速度蓄積をリセットし、坂のガタつきと過剰落下を防止する。
 *  - jumpRequested は接地時のみ受理し、jumpSpeed を初速として与える。
 *  - 計算結果は位置のみ Transform に書き戻す（回転は PlayerMoveSystem 側の責務）。
 * @note 並列実行（ForEachWithIDParallel）禁止。ループ内で非ロック版 BodyInterface
 *       へ書き込むため、並列化するとデータ競合でクラッシュする。
 */
void JoltCharacterUpdateSystem::Update(float dt) {
    if (!_world->HasResource<JoltPhysicsManager>())
        return;

    auto& joltManager = _world->GetResource<JoltPhysicsManager*>();
    JPH::PhysicsSystem* physicsSystem = joltManager->GetPhysicsSystem();
    JPH::BodyInterface& bodyInterface = joltManager->GetBodyInterface();

    const JPH::Vec3 gravity = physicsSystem->GetGravity();
    const JPH::Vec3 up(0.0f, 1.0f, 0.0f);

    // ※ Filter（IfSystem テンプレート）の宣言順 = ラムダ引数順。
    //    Write<Transform>, Read<Handle>, Read<Input>, Read<Config> の順に受け取る。
    ForEachWithID([&](CCL::ECS::EntityID id,
        TransformComponent& trans,
        const JoltCharacterHandleComponent& handle,
        const CharacterMovementInputComponent& input,
        const JoltCharacterConfigComponent& config) {
            if (!handle.character) return;
            JPH::CharacterVirtual* ch = handle.character.GetPtr();

            // ===============================================================
            // 自己無視フィルタ
            // ---------------------------------------------------------------
            // 本キャラは「CharacterVirtual（仮想）」と「JoltHandleComponent（実体の
            // Kinematic剛体＝幽霊）」の二重実体を持つ。CharacterVirtual の衝突計算が
            // 自分自身の幽霊剛体と当たらないよう、ここで除外する。
            // ※将来的には CharacterVirtualSettings::mInnerBodyShape による内部剛体の
            //   自動生成・自動同期へ寄せると、この手動同期と無視フィルタを撤廃できる。
            // ===============================================================
            JoltHandleComponent* kinBody = _world->GetComponent<JoltHandleComponent>(id);
            JPH::BodyID          ignoreBodyID = kinBody ? kinBody->bodyID : JPH::BodyID();
            JPH::IgnoreSingleBodyFilter bodyFilter(ignoreBodyID);

            // ===============================================================
            // 強制ワープ（リスポーン等）
            // ===============================================================
            if (trans.isTeleported) {
                ch->SetPosition(JPH::RVec3(trans.position.x, trans.position.y, trans.position.z));
                ch->SetRotation(JPH::Quat(trans.rotation.x, trans.rotation.y,
                    trans.rotation.z, trans.rotation.w));
                trans.isTeleported = false;
            }

            // ===============================================================
            // 速度の構築：水平＝入力、鉛直＝重力積分＋接地リセット＋ジャンプ
            // ===============================================================
            const bool onGround =
                ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;

            // 現在の鉛直速度成分を取り出す（水平は input で完全上書きするため）
            JPH::Vec3 cur = ch->GetLinearVelocity();
            float     vy = cur.Dot(up);

            // 接地中の下向き蓄積をリセット（坂でのガタつき・めり込みを防止）
            if (onGround && vy < 0.0f) {
                vy = 0.0f;
            }

            // ジャンプは接地時のみ受理（空中2段ジャンプを防止）
            if (input.jumpRequested && onGround) {
                vy = config.jumpSpeed;
            }

            // 重力積分（★ ExtendedUpdate はやってくれないので必須）
            vy += gravity.GetY() * dt;

            // 水平は「ペダル(input)」で完全上書き ＝ フル空中制御。
            // 慣性や加速カーブを入れたい場合は、ここで現在の水平速度とブレンドする。
            JPH::Vec3 newVel(input.desiredVelocity.x, vy, input.desiredVelocity.z);
            ch->SetLinearVelocity(newVel);

            // ===============================================================
            // 地形干渉計算（壁ずり・段差乗り越え・床吸着）
            // ---------------------------------------------------------------
            // ・段差乗り越え高さ(maxStepHeight)を Config から反映し、死にパラメータ化を防ぐ。
            // ・TempAllocator は Manager が事前確保した1個を再利用する。
            //   ExtendedUpdate はこの呼び出し内で alloc/free を完結（LIFO）するため、
            //   シングルスレッド実行であれば毎フレーム malloc する必要はない。
            // ===============================================================
            JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
            updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, config.maxStepHeight, 0.0f);

            ch->ExtendedUpdate(
                dt, gravity, updateSettings,
                physicsSystem->GetDefaultBroadPhaseLayerFilter(PhysicsLayers::MOVING),
                physicsSystem->GetDefaultLayerFilter(PhysicsLayers::MOVING),
                bodyFilter,
                {},
                *joltManager->GetTempAllocator());

            // ===============================================================
            // 結果の同期：位置のみ Transform へ書き戻す（回転は同期しない）
            // ===============================================================
            JPH::RVec3 pos = ch->GetPosition();
            trans.position = { pos.GetX(), pos.GetY(), pos.GetZ() };
            trans.isStatic = false; // 行列の再計算を要求

            // ===============================================================
            // 内部Kinematic剛体（幽霊）を CharacterVirtual に追従させる
            // ---------------------------------------------------------------
            // 他の動的剛体・レイキャストがこのキャラに当たれるようにするための実体。
            // 回転は PlayerMoveSystem が決めた trans.rotation を使う（直立カプセルの向き）。
            // ===============================================================
            if (kinBody) {
                JPH::Quat joltRot(trans.rotation.x, trans.rotation.y,
                    trans.rotation.z, trans.rotation.w);
                joltRot = joltRot.Normalized();
                bodyInterface.SetPositionAndRotation(
                    kinBody->bodyID, pos, joltRot, JPH::EActivation::DontActivate);
            }
        });
}

// 物理の Step が走る前に終わらせる（PrePhysics）
REGISTER_LOGIC_SYSTEM(JoltCharacterUpdateSystem, Priority::LogicStage::L03_PrePhysics);