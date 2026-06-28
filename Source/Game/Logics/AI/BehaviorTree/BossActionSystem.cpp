/**
 * @file BossActionSystem.cpp
 */
#include "BossActionSystem.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include <cmath>


#include "Engine/Camera/Components/VirtualCameraComponents.h"
//#include "Engine/Camera/CameraSpringArmComponent.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// =========================================================
// ★ 角度の差を -180度 ～ 180度 (-PI ~ PI) に丸めるヘルパー関数
// (ファイルの上のほう、あるいは無名名前空間に追加してください)
// =========================================================
namespace {
    float WrapAngle(float angle) {
        while (angle > DirectX::XM_PI) angle -= DirectX::XM_2PI;
        while (angle < -DirectX::XM_PI) angle += DirectX::XM_2PI;
        return angle;
    }
}

// ============================================================================
// アニメーション用 ハッシュ定数 (ファイルスコープ)
// ============================================================================
namespace AnimHashes {
    const uint32_t SPEED = CCL::Utils::HashString("Speed");
    const uint32_t ATTACK = CCL::Utils::HashString("Attack");

    // 1つのアクションを2つのアニメーションフェーズに分割
    const uint32_t CHARGE_PREP = CCL::Utils::HashString("ChargePrep"); // 予備動作(タメ)用
    const uint32_t CHARGE_START = CCL::Utils::HashString("Charge");    // 突進開始(TackleStart)用

    // ブレーキ（減速）フェーズを通知するフラグ
    const uint32_t HASH_IS_BRAKING = CCL::Utils::HashString("IsBraking");

    const uint32_t JUMP_START = CCL::Utils::HashString("JumpStart");
    const uint32_t JUMP_END = CCL::Utils::HashString("JumpEnd");
    const uint32_t IS_AIRBORNE = CCL::Utils::HashString("IsAirborne");

    const uint32_t PHASE_START = CCL::Utils::HashString("PhaseStart");
    const uint32_t PHASE_RECALL = CCL::Utils::HashString("PhaseRecall");
    const uint32_t PHASE_FALL = CCL::Utils::HashString("PhaseFall");
    const uint32_t PHASE_END = CCL::Utils::HashString("PhaseEnd");

    const uint32_t EVADE_START = CCL::Utils::HashString("EvadeStart");
    const uint32_t EVADE_END = CCL::Utils::HashString("EvadeEnd");
    const uint32_t IS_EVADING = CCL::Utils::HashString("IsEvading");

    // 横移動(Strafe)用のアニメーションパラメータ
    const uint32_t STRAFE_LEFT_START = CCL::Utils::HashString("StrafeLeftStart");
    const uint32_t STRAFE_LEFT_END = CCL::Utils::HashString("StrafeLeftEnd");
    const uint32_t IS_STRAFING_LEFT = CCL::Utils::HashString("IsStrafingLeft");

    const uint32_t STRAFE_RIGHT_START = CCL::Utils::HashString("StrafeRightStart");
    const uint32_t STRAFE_RIGHT_END = CCL::Utils::HashString("StrafeRightEnd");
    const uint32_t IS_STRAFING_RIGHT = CCL::Utils::HashString("IsStrafingRight");

    const uint32_t WALK_START = CCL::Utils::HashString("WalkStart");
    const uint32_t WALK_END = CCL::Utils::HashString("WalkEnd");

    // 死亡用のアニメーションパラメータ
    const uint32_t DEAD_START = CCL::Utils::HashString("DeadStart");
    const uint32_t IS_DEAD = CCL::Utils::HashString("IsDead");


    // 特殊垂直落下着地アクション専用のアニメーショントリガー
    const uint32_t LEVITATE_START = CCL::Utils::HashString("LevitateStart"); // JumpToHover用
    const uint32_t HEAVY_LAND = CCL::Utils::HashString("HeavyLand");     // HoverToLand用

}



void BossActionSystem::Update(float rawDt) {

    ForEachWithIDParallel([this](CCL::ECS::EntityID bossId, TransformComponent& trans, AnimParametersComponent& animParams,
        BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, BehaviorTreeComponent& btComp,
        const BossCommandComponent& cmd, const TimeStateComponent& time) {

            // ★各自の時計から dt を算出（これでラムダ内の dt はすべて localDt になる）
            float dt = time.localDt;
            float currentSpeed = 0.0f;

            // --------------------------------------------------------------------
            // [ Phase 0 ] 毎フレームの入力リセット
            // --------------------------------------------------------------------
            moveInput.desiredVelocity = Vector3::Zero;
            moveInput.jumpRequested = false;
            //moveInput.customJumpVelocity = 0.0f;
            //moveInput.customGravity = 0.0f;

            //// ★究極のハック: ホバリング中ならデフォルトで重力を無視し、落下を防ぐ
            //moveInput.overrideVerticalVelocity = bossDef.isHovering;


            // --------------------------------------------------------------------
            // [ Phase 1 ] コマンドの受付と状態更新
            // --------------------------------------------------------------------
            AcceptNewCommand(bossDef, cmd, trans);


            // --------------------------------------------------------------------
            // [ Phase 2 ] 状態に応じた物理移動の実行 (目標速度 targetSpeed の決定)
            // --------------------------------------------------------------------
            // AIが今「出したい」目標速度
            float targetSpeed = 0.0f;
            switch (bossDef.currentState) {
            case BossActionState::None:
                ProcessIdle(bossDef, trans, cmd, dt);
                targetSpeed = 0.0f;
                break;

            case BossActionState::Move:
                ProcessMove(bossDef, moveInput, trans, cmd, dt, targetSpeed);
                break;

            case BossActionState::Melee:
            case BossActionState::Evade:
                ProcessEvade(bossDef, dt, targetSpeed);
                break;

            case BossActionState::Flinch:
                targetSpeed = 0.0f;
                bossDef.currentMoveSpeed = 0.0f; // 慣性を完全に殺し、その場でピタッと止める
                break;

            case BossActionState::Strafe:
                ProcessStrafe(bossDef, trans, cmd, dt, targetSpeed);
                break;
            case BossActionState::Dead:
                // ★追加: 死んだら足が滑らないようにピタッと止める
                targetSpeed = 0.0f;
                bossDef.currentMoveSpeed = 0.0f;
                break;
            default:
                break;
            }

            // --------------------------------------------------------------------
            // [ Phase 3 ] 慣性（Inertia）の適用（足滑り防止の要！）
            // --------------------------------------------------------------------
            if (bossDef.currentState == BossActionState::Move ||
                bossDef.currentState == BossActionState::Strafe ||
                bossDef.currentState == BossActionState::Charge ||
                bossDef.currentState == BossActionState::None ||
                bossDef.currentState == BossActionState::Melee ||
                bossDef.currentState == BossActionState::Evade) 
            {
                // チャージ時は急加速・急停止させたいため、加速度を2倍にブーストする
                float currentAccel = (bossDef.currentState == BossActionState::Charge) ? bossDef.acceleration * 2.5f : bossDef.acceleration;
                float currentDecel = (bossDef.currentState == BossActionState::Charge) ? bossDef.deceleration * 2.0f : bossDef.deceleration;

                // 目標速度に向かって、実際の速度を滑らかに加減速させる
                if (targetSpeed > bossDef.currentMoveSpeed) {
                    bossDef.currentMoveSpeed += currentAccel * dt;
                    if (bossDef.currentMoveSpeed > targetSpeed) bossDef.currentMoveSpeed = targetSpeed;
                }
                else if (targetSpeed < bossDef.currentMoveSpeed) {
                    bossDef.currentMoveSpeed -= currentDecel * dt;
                    if (bossDef.currentMoveSpeed < targetSpeed) bossDef.currentMoveSpeed = targetSpeed;
                }

                // 進行方向の決定（Charge時は一度決めたアクション方向、それ以外は動的な移動方向）
                Vector3 dir = (bossDef.currentState == BossActionState::Charge) ? bossDef.actionDirection : bossDef.moveDirection;

                // 加減速した「現実の速度」を物理エンジンに渡す
                if (bossDef.currentMoveSpeed > 0.01f) {
                    moveInput.desiredVelocity = dir * bossDef.currentMoveSpeed;
                }
                else {
                    moveInput.desiredVelocity = Vector3::Zero;
                }

                // アニメーションに渡す速度も、この「滑らかな物理速度」にする
                currentSpeed = bossDef.currentMoveSpeed;
            }

            // --------------------------------------------------------------------
            // [ Phase 4 ] アクションタイマーの消化
            // --------------------------------------------------------------------
            
            // 減算する前の時間を記憶しておく（トリガー検知用）
            float previousTimer = bossDef.actionTimer;

            if (bossDef.actionTimer > 0.0f) {
                bossDef.actionTimer -= dt;

                if (bossDef.actionTimer <= 0.0f) {
                    bossDef.actionTimer = 0.0f;
                    bossDef.currentState = BossActionState::None; // タイマーが尽きたら自動で待機に戻る
                }
            }

            // --------------------------------------------------------------------
            // [ Phase 5 ] アニメーターへのデータ送信
            // --------------------------------------------------------------------

            // ★究極のハック: カニ歩き中は、通常歩行(Move)への誤爆を防ぐためにSpeedを0として送信する
            float animSpeed = (bossDef.currentState == BossActionState::Strafe) ? 0.0f : currentSpeed;
            animParams.SetFloat(AnimHashes::SPEED, animSpeed);

            // フラグの初期化
            bool isAirborne = bossDef.isHovering;
            bool isBraking = false;
            bool isStrafingRightParam = false;
            bool isStrafingLeftParam = false;
            bool isEvadingParam = false;

            if (bossDef.currentState == BossActionState::Melee) {
                if (previousTimer >= BossTimings::Melee_AnimTrigger && bossDef.actionTimer < BossTimings::Melee_AnimTrigger)
                    animParams.SetTrigger(AnimHashes::ATTACK);
            }
            else if (bossDef.currentState == BossActionState::Charge) {
                // ① アクション受理の瞬間: CHARGE_PREP 
                if (previousTimer >= bossDef.currentChargeTotal && bossDef.actionTimer < bossDef.currentChargeTotal) {
                    animParams.SetTrigger(AnimHashes::CHARGE_PREP);
                }

                // ② 突進の予備動作（飛び上がり）開始の瞬間
                if (previousTimer >= bossDef.currentChargeAnimStart && bossDef.actionTimer < bossDef.currentChargeAnimStart) {
                    animParams.SetTrigger(AnimHashes::CHARGE_START);
                }

                // ③ ブレーキフェーズ (ここは 0.5s 固定のままでOK)
                if (bossDef.actionTimer <= BossTimings::Charge_BrakeTime) {
                    isBraking = true;
                }
            }
            else if (bossDef.currentState == BossActionState::JumpAttack) {
                if (previousTimer >= BossTimings::JumpAttack_StartTrigger && bossDef.actionTimer < BossTimings::JumpAttack_StartTrigger) {
                    animParams.SetTrigger(AnimHashes::JUMP_START);
                }
                isAirborne = (bossDef.actionTimer <= BossTimings::JumpAttack_Liftoff && bossDef.actionTimer > BossTimings::JumpAttack_Land);
                if (previousTimer >= BossTimings::JumpAttack_Land && bossDef.actionTimer < BossTimings::JumpAttack_Land) {
                    animParams.SetTrigger(AnimHashes::JUMP_END);
                }
            }
            else if (bossDef.currentState == BossActionState::Evade) {
                if (previousTimer >= BossTimings::Evade_Total && bossDef.actionTimer < BossTimings::Evade_Total) {
                    animParams.SetTrigger(AnimHashes::EVADE_START);
                }
                // ★修正: 直接SetBoolするのではなく、isEvadingParam に代入する
                isEvadingParam = (bossDef.actionTimer <= BossTimings::Evade_Airborne && bossDef.actionTimer > BossTimings::Evade_Land);

                if (previousTimer >= BossTimings::Evade_Land && bossDef.actionTimer < BossTimings::Evade_Land) {
                    animParams.SetTrigger(AnimHashes::EVADE_END);
                }
            }
            // ====================================================================
            // ★修正: 横移動のアニメーション同期とフラグ管理
            // ====================================================================
            else if (bossDef.currentState == BossActionState::Strafe) {

                // カニ歩きアクション中（Start, Loop, End）は常にフラグをTrueにしておく
                isStrafingRightParam = bossDef.isStrafingRight;
                isStrafingLeftParam = !bossDef.isStrafingRight;

                // [Start] アクション開始の瞬間にトリガー
                if (previousTimer >= BossTimings::Strafe_Duration && bossDef.actionTimer < BossTimings::Strafe_Duration) {
                    if (bossDef.isStrafingRight) animParams.SetTrigger(AnimHashes::STRAFE_RIGHT_START);
                    else animParams.SetTrigger(AnimHashes::STRAFE_LEFT_START);
                }

                // [End] アクション終了間際にトリガー
                if (previousTimer >= BossTimings::Strafe_EndTrigger && bossDef.actionTimer < BossTimings::Strafe_EndTrigger) {
                    if (bossDef.isStrafingRight) animParams.SetTrigger(AnimHashes::STRAFE_RIGHT_END);
                    else animParams.SetTrigger(AnimHashes::STRAFE_LEFT_END);
                }
            }
            else if (bossDef.currentState == BossActionState::Move) {
                // [Start] アクション開始の瞬間（AIがMoveを発注した最初のフレーム）
                if (previousTimer >= BossTimings::Move_Duration && bossDef.actionTimer < BossTimings::Move_Duration) {
                    animParams.SetTrigger(AnimHashes::WALK_START);
                }

                // [End] タイムアウト（AIが歩くのをやめた）間際
                // ※ Moveは途中で別の行動で上書きされることも多いため、確実にEndを踏ませたい場合は
                // 別のステートに移った瞬間に強制的に WALK_END を送るようなフックを入れるのも手だ。
                if (bossDef.actionTimer <= 0.1f) {
                    animParams.SetTrigger(AnimHashes::WALK_END);
                }
            }
            else if (bossDef.currentState == BossActionState::Dead) {

                // [ 1. 怯み ] : 死亡が確定した最初の瞬間 (3.0s -> 2.5s)
                if (previousTimer >= BossTimings::Death_TotalDuration && bossDef.actionTimer < BossTimings::Death_TotalDuration) {
                    animParams.SetTrigger(CCL::Utils::HashString("Damage"));
                }

                // [ 2. 倒れ込み ] : 怯みが終わった瞬間 (2.5s) に発動
                if (previousTimer >= BossTimings::Death_FlinchTime && bossDef.actionTimer < BossTimings::Death_FlinchTime) {
                    animParams.SetTrigger(AnimHashes::DEAD_START);
                }

                // [ 3. 死亡完了 ] : 全アニメーション終了後、地面に横たわるループへ
                if (bossDef.actionTimer <= 0.0f) {
                    animParams.SetBool(AnimHashes::IS_DEAD, true);

                    // ※スローモーションを解除する場合はここで行う
                    // if (_world->HasResource<TimeContext>()) _world->GetResource<TimeContext>().globalScale = 1.0f;
                }
            }
            // ====================================================================
            // [ 飛び上がりアクション (JumpToHover) ]
            // アクション開始時に「LevitateStart（浮遊開始）」をトリガーする
            // ====================================================================
            else if (bossDef.currentState == BossActionState::JumpToHover) {
                if (previousTimer >= BossTimings::JumpToHover_Total && bossDef.actionTimer < BossTimings::JumpToHover_Total) {
                    animParams.SetTrigger(AnimHashes::LEVITATE_START); // ★修正
                }
                isAirborne = (bossDef.actionTimer <= BossTimings::JumpToHover_Liftoff);
            }
            // ====================================================================
            // [ 垂直落下アクション (HoverToLand) ]
            // 落下完了（地面激突）の瞬間に「HeavyLand（重力着地）」をトリガーする
            // ====================================================================
            else if (bossDef.currentState == BossActionState::HoverToLand) {
                isAirborne = (bossDef.actionTimer > BossTimings::HoverToLand_Land);

                // 着地の瞬間のみトリガーを送信（落下開始のトリガーは送らない）
                if (previousTimer >= BossTimings::HoverToLand_Land && bossDef.actionTimer < BossTimings::HoverToLand_Land) {
                    animParams.SetTrigger(AnimHashes::HEAVY_LAND); // ★修正
                }
            }
            else if (bossDef.currentState == BossActionState::EscapeJump) {
                // 1.5s ～ 0.5s の間を空中とする
                isAirborne = (bossDef.actionTimer <= 1.5f && bossDef.actionTimer > 0.5f);

                // 1.5s の瞬間にジャンプトリガー
                if (previousTimer >= 1.5f && bossDef.actionTimer < 1.5f) {
                    animParams.SetTrigger(AnimHashes::JUMP_START);
                }

                // 0.5s の着地の瞬間に着地トリガー
                if (previousTimer >= 0.5f && bossDef.actionTimer < 0.5f) {
                    animParams.SetTrigger(AnimHashes::JUMP_END);

                    // 着地時のカメラシェイクを鳴らしたい場合はここに入れる
                }
                }
                

            // 最後に一括で送信
            animParams.SetBool(AnimHashes::IS_AIRBORNE, isAirborne);
            animParams.SetBool(AnimHashes::HASH_IS_BRAKING, isBraking);
            animParams.SetBool(AnimHashes::IS_STRAFING_RIGHT, isStrafingRightParam);
            animParams.SetBool(AnimHashes::IS_STRAFING_LEFT, isStrafingLeftParam);
            animParams.SetBool(AnimHashes::IS_EVADING, isEvadingParam);
        });
}


// ============================================================================
// 各アクションの具体的な処理（ヘルパー関数）
// ============================================================================

void BossActionSystem::AcceptNewCommand(BossActionComponent& bossDef, const BossCommandComponent& cmd, TransformComponent& trans) {
    // =======================================================
    // 大技の硬直中、および「怯み(Flinch)中」はAIからの新しい命令を完全に無視する
    // =======================================================
    if (bossDef.currentState != BossActionState::None && bossDef.currentState != BossActionState::Move) {
        return; // 硬直中・怯み中ならAIの命令は届かない（脊髄反射優先）
    }

    bossDef.currentState = BossActionState::None;

    switch (cmd.currentActionId) {

    case AI::A_MeleeAttack: {
        bossDef.currentState = BossActionState::Melee;
        // =======================================================
        // ★修正: 1.0f ではなく、BossTimings 定数を代入！
        // =======================================================
        bossDef.actionTimer = BossTimings::Melee_Duration;

        const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
        if (playerTrans) {
            Vector3 dir = playerTrans->position - trans.position;
            dir.y = 0.0f;
            if (dir.LengthSquared() > 0.01f) {
                dir.Normalize();
                float targetYaw = atan2f(dir.x, dir.z);
                trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
                trans.isDirty = true;
            }
        }
        break;
    }
    case AI::A_StrafeLeft:
    case AI::A_StrafeRight:
    case AI::A_StrafeRandom: {
        bossDef.currentState = BossActionState::Strafe;
        bossDef.actionTimer = BossTimings::Strafe_Duration;

        // ★ ランダム性の決定
        if (cmd.currentActionId == AI::A_StrafeRight) {
            bossDef.isStrafingRight = true;
        }
        else if (cmd.currentActionId == AI::A_StrafeLeft) {
            bossDef.isStrafingRight = false;
        }
        else {
            // C++の標準的なランダム (0 か 1)
            bossDef.isStrafingRight = (rand() % 2 == 0);
        }
        break;
    }
    case AI::A_ChargeAttack: {
        bossDef.currentState = BossActionState::Charge;
        bossDef.actionTimer = bossDef.currentChargeTotal;
        bossDef.jumpRequested = true;

        // ★アーキテクチャ修正: 発動した瞬間のボスの位置を「世界のアンカー」として完全固定！
        bossDef.chargeAnchorPosition = trans.position;

        // （以下、前回の方向固定ロジック。基準を固定座標に変更してより頑健にする）
        const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
        if (playerTrans) {
            Vector3 targetPos = playerTrans->position;
            targetPos.y -= 0.5f;

            // ロックされた固定座標からのベクトルを計算
            Vector3 dir = targetPos - bossDef.chargeAnchorPosition;
            if (dir.y > 0.0f) dir.y = -0.1f;

            if (dir.LengthSquared() > 0.01f) {
                dir.Normalize();
                bossDef.actionDirection = dir;
            }
            else {
                bossDef.actionDirection = Vector3::Transform(Vector3::UnitZ, trans.rotation);
            }
        }
        break;
    }
    case AI::A_Move: {
        bossDef.currentState = BossActionState::Move;

        // ====================================================================
        // ★究極のハック: Moveに「思考の寿命（タイムアウト）」を持たせる！
        // 0.5秒間歩いたら一旦 None に戻り、BTに「次は何をする？」と強制的に再評価させる。
        // ====================================================================
        bossDef.actionTimer = 0.5f;

        break;
    }
    case AI::A_JumpAttack: {
        bossDef.currentState = BossActionState::JumpAttack;
        bossDef.actionTimer = BossTimings::JumpAttack_Total; // (ここは既に定数になっていましたわね)
        // ★修正: ここではまだジャンプフラグを立てない！（タメ中に飛んでしまうため）
        bossDef.jumpRequested = true; // ← このフラグは ProcessJumpAttack でタイミングを見て消費します

        const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
        if (playerTrans) {
            Vector3 dir = playerTrans->position - trans.position;
            dir.y = 0.0f;
            float distance = dir.Length();

            if (distance > 0.01f) {
                dir.Normalize();
                bossDef.actionDirection = dir;
                bossDef.dynamicJumpSpeed = distance / 1.5f;

                float targetYaw = atan2f(dir.x, dir.z);
                trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
                trans.isDirty = true;
            }
        }
        break;
    }
    case AI::A_EscapeToCenter: {
        bossDef.currentState = BossActionState::EscapeJump;
        bossDef.actionTimer = BossTimings::EscapeToCenter_Total; // 全体タイム 2.0秒
        bossDef.jumpRequested = true;
        bossDef.currentMoveSpeed = 0.0f;
        break;
    }
    case AI::A_EvadeBackward: {
        bossDef.currentState = BossActionState::Evade;
        // 新しい Total タイマーを使う
        bossDef.actionTimer = BossTimings::Evade_Total;

        const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
        if (playerTrans) {
            // =========================================================
            // ★ 逆ベクトルの計算: 「自分の位置 - プレイヤーの位置」
            // =========================================================
            Vector3 dir = trans.position - playerTrans->position;
            dir.y = 0.0f;
            if (dir.LengthSquared() > 0.01f) {
                dir.Normalize();
                bossDef.actionDirection = dir; // 逃げる方向を記憶

                // ※ボスの体（Rotation）はプレイヤーの方を向けたまま（ガン見しながら）後ろに下がる
                float targetYaw = atan2f(-dir.x, -dir.z);
                trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
                trans.isDirty = true;
            }
        }
        break;
    }
    case AI::A_PhaseTransition: {
        bossDef.currentState = BossActionState::PhaseTransition;
        bossDef.actionTimer = BossTimings::PhaseTransition_Total; // 5.0秒
        break;
    }
    case AI::A_JumpToHover: {
        bossDef.currentState = BossActionState::JumpToHover;
        bossDef.actionTimer = BossTimings::JumpToHover_Total;
        bossDef.jumpRequested = true;
        break;
    }
    case AI::A_HoverToLand: {
        bossDef.currentState = BossActionState::HoverToLand;
        bossDef.actionTimer = BossTimings::HoverToLand_Total;
        break;
    }
    default:
        break;
    }
}


// ============================================================================
// 【アクション名】 歩行・追従移動 (Move)
// 【調整パラメータ (BossActionComponent)】
//  - walkSpeed : プレイヤーへ近づく際の移動速度
//  - turnSpeed : プレイヤーの方向を向く際の旋回速度
// 【動作仕様】
//   プレイヤーの現在位置へ向かって一定速度で歩行し、同時に滑らかに旋回して向きを合わせる。
// ============================================================================
void BossActionSystem::ProcessMove(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, const BossCommandComponent& cmd, float dt, float& outTargetSpeed) {
    const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
    if (!playerTrans) return;

    Vector3 moveDir = playerTrans->position - trans.position;
    moveDir.y = 0.0f;

    if (moveDir.LengthSquared() > 0.01f) {
        moveDir.Normalize();
        outTargetSpeed = bossDef.walkSpeed; // ★目標速度(TargetSpeed)を設定

        bossDef.moveDirection = moveDir;

        // 1. 目標の角度 (Target Yaw)
        float targetYaw = atan2f(moveDir.x, moveDir.z);

        // 2. 現在の角度 (Current Yaw) を取得
        Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
        float currentYaw = atan2f(currentForward.x, currentForward.z);

        // 3. 最短の回転方向を計算 (-180度 ～ 180度 に丸める)
        float diff = WrapAngle(targetYaw - currentYaw);

        // 4. 一定速度で角度を近づける
        float speed = (bossDef.turnSpeed <= 0.0f) ? 10.0f : bossDef.turnSpeed;
        float newYaw = currentYaw + diff * (speed * dt);

        // 5. 計算した新しい角度から、純粋なY軸回転のクォータニオンを再構築
        trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, newYaw);
        trans.isDirty = true;
    }
}

// ============================================================================
// 【アクション名】 回り込み・横移動 (Circle Strafe)
// 【動作仕様】
//   プレイヤーへのベクトルと上方向(Y)の外積を利用し、プレイヤーを中心とした
//   円周上を移動（カニ歩き）する。体は常にプレイヤーの方を睨み続ける。
// ============================================================================
void BossActionSystem::ProcessStrafe(BossActionComponent& bossDef, TransformComponent& trans, const BossCommandComponent& cmd, float dt, float& outTargetSpeed) {
    const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
    if (!playerTrans) return;

    // 1. プレイヤーへのベクトルを出す
    Vector3 toPlayer = playerTrans->position - trans.position;
    toPlayer.y = 0.0f;

    if (toPlayer.LengthSquared() > 0.01f) {
        toPlayer.Normalize();

        // ====================================================================
        // ★ 外積(Cross Product)による「完璧な横方向」の算出
        // ====================================================================
        // プレイヤーへのベクトル と 真上(Y) の外積を取ると、必ず「右」のベクトルが出る
        Vector3 rightDir = toPlayer.Cross(Vector3::UnitY);

        // 決定されたフラグに従って移動方向を決める
        bossDef.moveDirection = bossDef.isStrafingRight ? rightDir : -rightDir;

        // ====================================================================
        // アニメーション(Start/Loop/End)に合わせた速度カーブの適用
        // ====================================================================
        if (bossDef.actionTimer > BossTimings::Strafe_LoopStart) {
            // [Start] 踏み込みのモーション中なので、最高速を出さずタメを作る
            outTargetSpeed = bossDef.strafeSpeed * 0.5f;
        }
        else if (bossDef.actionTimer <= BossTimings::Strafe_EndTrigger) {
            // [End] ブレーキをかけて立ち止まるモーション中なので、足を止める
            outTargetSpeed = 0.0f;
        }
        else {
            // [Loop] カニ歩きの巡回速度
            outTargetSpeed = bossDef.strafeSpeed;
        }

        // ====================================================================
        // ★ 移動しながらプレイヤーを睨み続ける（旋回処理）
        // ====================================================================
        float targetYaw = atan2f(toPlayer.x, toPlayer.z);
        Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
        float currentYaw = atan2f(currentForward.x, currentForward.z);

        float diff = WrapAngle(targetYaw - currentYaw);
        float speed = (bossDef.turnSpeed <= 0.0f) ? 10.0f : bossDef.turnSpeed;
        float newYaw = currentYaw + diff * (speed * dt);

        trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, newYaw);
        trans.isDirty = true;
    }
}

// ============================================================================
// 【アクション名】 バックステップ回避 (Evade)
// 【調整パラメータ (BossActionComponent)】
//  - evadeSpeed : 後ろへ飛び退くスピード
// 【動作仕様】
//   現在向いている方向の「真後ろ（-Z方向）」に対して、重力を無視して素早く後退する。
//   無敵時間や怯み抜けなどと組み合わせて使用される。
// ============================================================================
void BossActionSystem::ProcessEvade(BossActionComponent& bossDef, float dt, float& outTargetSpeed) {
    // Start(タメ)とEnd(着地)の最中は動かず、Loop(空中)の期間だけ物理的に後ろへ下がる
    if (bossDef.actionTimer <= BossTimings::Evade_Airborne && bossDef.actionTimer > BossTimings::Evade_Land) {

        float airborneDuration = BossTimings::Evade_Airborne - BossTimings::Evade_Land;
        float progress = (bossDef.actionTimer - BossTimings::Evade_Land) / airborneDuration;

        // 2次関数でカーブを作る (1.0 -> 0.0)
        outTargetSpeed = bossDef.evadeSpeed * (progress * progress);

        // ====================================================================
        // ★ 究極の自然な回避カーブ (Lerpによる初速のブレンド)
        // ====================================================================
        // いきなりトップギアに入れるとワープして見えてしまうため、
        // 「現在の速度」と「目標の最高速度」を非常に短い時間（Lerp）で滑らかに繋ぎます。
        // これにより、地面を「グッ、ダン！」と蹴る重みのある初動が生まれます。

        float kickPower = 15.0f; // 地面を蹴る力（大きいほど素早く最高速に到達する）

        if (bossDef.currentMoveSpeed < outTargetSpeed) {
            // 現在速度を、最高速に向かって急速に引っ張り上げる
            bossDef.currentMoveSpeed = std::lerp(bossDef.currentMoveSpeed, outTargetSpeed, kickPower * dt);
        }

        bossDef.moveDirection = bossDef.actionDirection;
    }
    else {
        outTargetSpeed = 0.0f; // 地面に足がついている時は急ブレーキ
        bossDef.moveDirection = bossDef.actionDirection;
    }
}

// ============================================================================
// 【アクション名】 待機・注視 (Idle)
// 【調整パラメータ (BossActionComponent)】
//  - turnSpeed : その場でプレイヤーを目で追う（旋回する）スピード
// 【動作仕様】
//   移動速度はゼロのまま、プレイヤーの方向を滑らかに向き続ける。
// ============================================================================
void BossActionSystem::ProcessIdle(BossActionComponent& bossDef, TransformComponent& trans, const BossCommandComponent& cmd, float dt) {
    const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
    if (!playerTrans) return;

    Vector3 dirToPlayer = playerTrans->position - trans.position;
    dirToPlayer.y = 0.0f; // 水平方向のみで計算

    // プレイヤーと完全に重なっていない場合のみ計算
    if (dirToPlayer.LengthSquared() > 0.01f) {

        // 1. プレイヤーの方向の角度 (Target Yaw) を計算
        float targetYaw = atan2f(dirToPlayer.x, dirToPlayer.z);

        // 2. 現在の自分の角度 (Current Yaw) を取得
        Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
        float currentYaw = atan2f(currentForward.x, currentForward.z);

        // 3. 最短の回転方向を計算 (-180度 ～ 180度 に丸める)
        float diff = WrapAngle(targetYaw - currentYaw);

        // ====================================================================
        // ★演出の極意：待機中の旋回は、歩行中(turnSpeed)の半分にする
        // すぐにビュッと向くのではなく、重たい体でジリジリと獲物を睨みつける演出
        // ====================================================================
        float idleTurnSpeed = (bossDef.turnSpeed <= 0.0f) ? 10.0f : (bossDef.turnSpeed * 0.5f);

        // 4. 一定速度で角度を近づける
        float newYaw = currentYaw + diff * (idleTurnSpeed * dt);

        // 5. 新しい角度をクォータニオンとして保存
        trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, newYaw);
        trans.isDirty = true;
    }
}




REGISTER_LOGIC_SYSTEM(BossActionSystem, Priority::LogicStage::L04_Physics);