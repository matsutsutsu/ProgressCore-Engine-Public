///**
// * @file BossActionSystem.cpp
// */
//#include "BossActionSystem.h"
//#include "Engine/Core/Math/StringHash.h"
//#include "Game/Core/SystemPriority.h"
//#include "Engine/ECS/System/CCL_SystemRegistry.h"
//#include "Game/Logic/AI/BehaviorTree/Data/ActionRegistry.h"
//#include "Engine/Serialization/Factory/Prefab.h"
//#include <cmath>
//#include "Game/Core/PrefabSpawnQueue.h"
//
//#include "Engine/Camera/Components/VirtualCameraComponents.h"
//#include "Engine/Camera/CameraSpringArmComponent.h"
//
//using namespace CCL::ECS;
//using namespace DirectX::SimpleMath;
//
//// =========================================================
//// ★ 角度の差を -180度 ～ 180度 (-PI ~ PI) に丸めるヘルパー関数
//// (ファイルの上のほう、あるいは無名名前空間に追加してください)
//// =========================================================
//namespace {
//    float WrapAngle(float angle) {
//        while (angle > DirectX::XM_PI) angle -= DirectX::XM_2PI;
//        while (angle < -DirectX::XM_PI) angle += DirectX::XM_2PI;
//        return angle;
//    }
//}
//
//// ============================================================================
//// アニメーション用 ハッシュ定数 (ファイルスコープ)
//// ============================================================================
//namespace AnimHashes {
//    const uint32_t SPEED = CCL::Utils::HashString("Speed");
//    const uint32_t ATTACK = CCL::Utils::HashString("Attack");
//
//    // 1つのアクションを2つのアニメーションフェーズに分割
//    const uint32_t CHARGE_PREP = CCL::Utils::HashString("ChargePrep"); // 予備動作(タメ)用
//    const uint32_t CHARGE_START = CCL::Utils::HashString("Charge");    // 突進開始(TackleStart)用
//
//    // ブレーキ（減速）フェーズを通知するフラグ
//    const uint32_t HASH_IS_BRAKING = CCL::Utils::HashString("IsBraking");
//
//    const uint32_t JUMP_START = CCL::Utils::HashString("JumpStart");
//    const uint32_t JUMP_END = CCL::Utils::HashString("JumpEnd");
//    const uint32_t IS_AIRBORNE = CCL::Utils::HashString("IsAirborne");
//
//    const uint32_t PHASE_START = CCL::Utils::HashString("PhaseStart");
//    const uint32_t PHASE_RECALL = CCL::Utils::HashString("PhaseRecall");
//    const uint32_t PHASE_FALL = CCL::Utils::HashString("PhaseFall");
//    const uint32_t PHASE_END = CCL::Utils::HashString("PhaseEnd");
//
//    const uint32_t EVADE_START = CCL::Utils::HashString("EvadeStart");
//    const uint32_t EVADE_END = CCL::Utils::HashString("EvadeEnd");
//    const uint32_t IS_EVADING = CCL::Utils::HashString("IsEvading");
//
//    // 横移動(Strafe)用のアニメーションパラメータ
//    const uint32_t STRAFE_LEFT_START = CCL::Utils::HashString("StrafeLeftStart");
//    const uint32_t STRAFE_LEFT_END = CCL::Utils::HashString("StrafeLeftEnd");
//    const uint32_t IS_STRAFING_LEFT = CCL::Utils::HashString("IsStrafingLeft");
//
//    const uint32_t STRAFE_RIGHT_START = CCL::Utils::HashString("StrafeRightStart");
//    const uint32_t STRAFE_RIGHT_END = CCL::Utils::HashString("StrafeRightEnd");
//    const uint32_t IS_STRAFING_RIGHT = CCL::Utils::HashString("IsStrafingRight");
//
//    const uint32_t WALK_START = CCL::Utils::HashString("WalkStart");
//    const uint32_t WALK_END = CCL::Utils::HashString("WalkEnd");
//
//    // 死亡用のアニメーションパラメータ
//    const uint32_t DEAD_START = CCL::Utils::HashString("DeadStart");
//    const uint32_t IS_DEAD = CCL::Utils::HashString("IsDead");
//
//
//    // 特殊垂直落下着地アクション専用のアニメーショントリガー
//    const uint32_t LEVITATE_START = CCL::Utils::HashString("LevitateStart"); // JumpToHover用
//    const uint32_t HEAVY_LAND = CCL::Utils::HashString("HeavyLand");     // HoverToLand用
//
//}
//
//
//
//void BossActionSystem::Update(float rawDt) {
//
//    ForEachWithIDParallel([this](CCL::ECS::EntityID bossId, TransformComponent& trans, AnimParametersComponent& animParams,
//        BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, BehaviorTreeComponent& btComp,
//        const BossCommandComponent& cmd, const TimeState& time) {
//
//            // ★各自の時計から dt を算出（これでラムダ内の dt はすべて localDt になる）
//            float dt = time.localDt;
//            float currentSpeed = 0.0f;
//
//            // --------------------------------------------------------------------
//            // [ Phase 0 ] 毎フレームの入力リセット
//            // --------------------------------------------------------------------
//            moveInput.desiredVelocity = Vector3::Zero;
//            moveInput.jumpRequested = false;
//            moveInput.customJumpVelocity = 0.0f;
//            moveInput.customGravity = 0.0f;
//
//            // ★究極のハック: ホバリング中ならデフォルトで重力を無視し、落下を防ぐ
//            moveInput.overrideVerticalVelocity = bossDef.isHovering;
//
//
//            // --------------------------------------------------------------------
//            // [ Phase 1 ] コマンドの受付と状態更新
//            // --------------------------------------------------------------------
//            AcceptNewCommand(bossDef, cmd, trans);
//
//
//            // --------------------------------------------------------------------
//            // [ Phase 2 ] 状態に応じた物理移動の実行 (目標速度 targetSpeed の決定)
//            // --------------------------------------------------------------------
//            // AIが今「出したい」目標速度
//            float targetSpeed = 0.0f;
//            switch (bossDef.currentState) {
//            case BossActionState::None:
//                ProcessIdle(bossDef, trans, cmd, dt);
//                targetSpeed = 0.0f;
//                break;
//
//            case BossActionState::Move:
//                ProcessMove(bossDef, moveInput, trans, cmd, dt, targetSpeed);
//                break;
//
//            case BossActionState::Charge:
//                ProcessCharge(bossDef, moveInput, trans, cmd, dt, targetSpeed);
//                break;
//
//            case BossActionState::JumpAttack:
//                ProcessJumpAttack(bossDef, moveInput, trans, dt, targetSpeed);
//                bossDef.currentMoveSpeed = 0.0f; // 大技中は慣性をリセット
//                break;
//            case BossActionState::EscapeJump:
//                ProcessEscapeJump(bossDef, moveInput, trans, dt, targetSpeed);
//                bossDef.currentMoveSpeed = 0.0f; // 慣性システムに邪魔されないよう強制0
//                break;
//            case BossActionState::Melee:
//            case BossActionState::Evade:
//                ProcessEvade(bossDef, dt, targetSpeed);
//                break;
//
//            case BossActionState::Flinch:
//                targetSpeed = 0.0f;
//                bossDef.currentMoveSpeed = 0.0f; // 慣性を完全に殺し、その場でピタッと止める
//                break;
//
//            case BossActionState::PhaseTransition:
//                ProcessPhaseTransition(bossId, bossDef, moveInput, trans,btComp, dt,targetSpeed);
//                targetSpeed = 0.0f;
//                break;
//
//            case BossActionState::JumpToHover:
//                ProcessJumpToHover(bossDef, moveInput, trans, dt, targetSpeed);
//                bossDef.currentMoveSpeed = 0.0f;
//                break;
//
//            case BossActionState::HoverToLand:
//                ProcessHoverToLand(bossDef, moveInput, trans, cmd, dt, targetSpeed);
//                bossDef.currentMoveSpeed = 0.0f;
//                break;
//            case BossActionState::Strafe:
//                ProcessStrafe(bossDef, trans, cmd, dt, targetSpeed);
//                break;
//            case BossActionState::Dead:
//                // ★追加: 死んだら足が滑らないようにピタッと止める
//                targetSpeed = 0.0f;
//                bossDef.currentMoveSpeed = 0.0f;
//                break;
//            default:
//                break;
//            }
//
//            // --------------------------------------------------------------------
//            // [ Phase 3 ] 慣性（Inertia）の適用（足滑り防止の要！）
//            // --------------------------------------------------------------------
//            if (bossDef.currentState == BossActionState::Move ||
//                bossDef.currentState == BossActionState::Strafe ||
//                bossDef.currentState == BossActionState::Charge ||
//                bossDef.currentState == BossActionState::None ||
//                bossDef.currentState == BossActionState::Melee ||
//                bossDef.currentState == BossActionState::Evade) 
//            {
//                // チャージ時は急加速・急停止させたいため、加速度を2倍にブーストする
//                float currentAccel = (bossDef.currentState == BossActionState::Charge) ? bossDef.acceleration * 2.5f : bossDef.acceleration;
//                float currentDecel = (bossDef.currentState == BossActionState::Charge) ? bossDef.deceleration * 2.0f : bossDef.deceleration;
//
//                // 目標速度に向かって、実際の速度を滑らかに加減速させる
//                if (targetSpeed > bossDef.currentMoveSpeed) {
//                    bossDef.currentMoveSpeed += currentAccel * dt;
//                    if (bossDef.currentMoveSpeed > targetSpeed) bossDef.currentMoveSpeed = targetSpeed;
//                }
//                else if (targetSpeed < bossDef.currentMoveSpeed) {
//                    bossDef.currentMoveSpeed -= currentDecel * dt;
//                    if (bossDef.currentMoveSpeed < targetSpeed) bossDef.currentMoveSpeed = targetSpeed;
//                }
//
//                // 進行方向の決定（Charge時は一度決めたアクション方向、それ以外は動的な移動方向）
//                Vector3 dir = (bossDef.currentState == BossActionState::Charge) ? bossDef.actionDirection : bossDef.moveDirection;
//
//                // 加減速した「現実の速度」を物理エンジンに渡す
//                if (bossDef.currentMoveSpeed > 0.01f) {
//                    moveInput.desiredVelocity = dir * bossDef.currentMoveSpeed;
//                }
//                else {
//                    moveInput.desiredVelocity = Vector3::Zero;
//                }
//
//                // アニメーションに渡す速度も、この「滑らかな物理速度」にする
//                currentSpeed = bossDef.currentMoveSpeed;
//            }
//
//            // --------------------------------------------------------------------
//            // [ Phase 4 ] アクションタイマーの消化
//            // --------------------------------------------------------------------
//            
//            // 減算する前の時間を記憶しておく（トリガー検知用）
//            float previousTimer = bossDef.actionTimer;
//
//            if (bossDef.actionTimer > 0.0f) {
//                bossDef.actionTimer -= dt;
//
//                if (bossDef.actionTimer <= 0.0f) {
//                    bossDef.actionTimer = 0.0f;
//                    bossDef.currentState = BossActionState::None; // タイマーが尽きたら自動で待機に戻る
//                }
//            }
//
//            // --------------------------------------------------------------------
//            // [ Phase 5 ] アニメーターへのデータ送信
//            // --------------------------------------------------------------------
//
//            // ★究極のハック: カニ歩き中は、通常歩行(Move)への誤爆を防ぐためにSpeedを0として送信する
//            float animSpeed = (bossDef.currentState == BossActionState::Strafe) ? 0.0f : currentSpeed;
//            animParams.SetFloat(AnimHashes::SPEED, animSpeed);
//
//            // フラグの初期化
//            bool isAirborne = bossDef.isHovering;
//            bool isBraking = false;
//            bool isStrafingRightParam = false;
//            bool isStrafingLeftParam = false;
//            bool isEvadingParam = false;
//
//            if (bossDef.currentState == BossActionState::Melee) {
//                if (previousTimer >= BossTimings::Melee_AnimTrigger && bossDef.actionTimer < BossTimings::Melee_AnimTrigger)
//                    animParams.SetTrigger(AnimHashes::ATTACK);
//            }
//            else if (bossDef.currentState == BossActionState::Charge) {
//                // ① アクション受理の瞬間: CHARGE_PREP 
//                if (previousTimer >= bossDef.currentChargeTotal && bossDef.actionTimer < bossDef.currentChargeTotal) {
//                    animParams.SetTrigger(AnimHashes::CHARGE_PREP);
//                }
//
//                // ② 突進の予備動作（飛び上がり）開始の瞬間
//                if (previousTimer >= bossDef.currentChargeAnimStart && bossDef.actionTimer < bossDef.currentChargeAnimStart) {
//                    animParams.SetTrigger(AnimHashes::CHARGE_START);
//                }
//
//                // ③ ブレーキフェーズ (ここは 0.5s 固定のままでOK)
//                if (bossDef.actionTimer <= BossTimings::Charge_BrakeTime) {
//                    isBraking = true;
//                }
//            }
//            else if (bossDef.currentState == BossActionState::JumpAttack) {
//                if (previousTimer >= BossTimings::JumpAttack_StartTrigger && bossDef.actionTimer < BossTimings::JumpAttack_StartTrigger) {
//                    animParams.SetTrigger(AnimHashes::JUMP_START);
//                }
//                isAirborne = (bossDef.actionTimer <= BossTimings::JumpAttack_Liftoff && bossDef.actionTimer > BossTimings::JumpAttack_Land);
//                if (previousTimer >= BossTimings::JumpAttack_Land && bossDef.actionTimer < BossTimings::JumpAttack_Land) {
//                    animParams.SetTrigger(AnimHashes::JUMP_END);
//                }
//            }
//            else if (bossDef.currentState == BossActionState::Evade) {
//                if (previousTimer >= BossTimings::Evade_Total && bossDef.actionTimer < BossTimings::Evade_Total) {
//                    animParams.SetTrigger(AnimHashes::EVADE_START);
//                }
//                // ★修正: 直接SetBoolするのではなく、isEvadingParam に代入する
//                isEvadingParam = (bossDef.actionTimer <= BossTimings::Evade_Airborne && bossDef.actionTimer > BossTimings::Evade_Land);
//
//                if (previousTimer >= BossTimings::Evade_Land && bossDef.actionTimer < BossTimings::Evade_Land) {
//                    animParams.SetTrigger(AnimHashes::EVADE_END);
//                }
//            }
//            // ====================================================================
//            // ★修正: 横移動のアニメーション同期とフラグ管理
//            // ====================================================================
//            else if (bossDef.currentState == BossActionState::Strafe) {
//
//                // カニ歩きアクション中（Start, Loop, End）は常にフラグをTrueにしておく
//                isStrafingRightParam = bossDef.isStrafingRight;
//                isStrafingLeftParam = !bossDef.isStrafingRight;
//
//                // [Start] アクション開始の瞬間にトリガー
//                if (previousTimer >= BossTimings::Strafe_Duration && bossDef.actionTimer < BossTimings::Strafe_Duration) {
//                    if (bossDef.isStrafingRight) animParams.SetTrigger(AnimHashes::STRAFE_RIGHT_START);
//                    else animParams.SetTrigger(AnimHashes::STRAFE_LEFT_START);
//                }
//
//                // [End] アクション終了間際にトリガー
//                if (previousTimer >= BossTimings::Strafe_EndTrigger && bossDef.actionTimer < BossTimings::Strafe_EndTrigger) {
//                    if (bossDef.isStrafingRight) animParams.SetTrigger(AnimHashes::STRAFE_RIGHT_END);
//                    else animParams.SetTrigger(AnimHashes::STRAFE_LEFT_END);
//                }
//            }
//            else if (bossDef.currentState == BossActionState::Move) {
//                // [Start] アクション開始の瞬間（AIがMoveを発注した最初のフレーム）
//                if (previousTimer >= BossTimings::Move_Duration && bossDef.actionTimer < BossTimings::Move_Duration) {
//                    animParams.SetTrigger(AnimHashes::WALK_START);
//                }
//
//                // [End] タイムアウト（AIが歩くのをやめた）間際
//                // ※ Moveは途中で別の行動で上書きされることも多いため、確実にEndを踏ませたい場合は
//                // 別のステートに移った瞬間に強制的に WALK_END を送るようなフックを入れるのも手だ。
//                if (bossDef.actionTimer <= 0.1f) {
//                    animParams.SetTrigger(AnimHashes::WALK_END);
//                }
//            }
//            else if (bossDef.currentState == BossActionState::Dead) {
//
//                // [ 1. 怯み ] : 死亡が確定した最初の瞬間 (3.0s -> 2.5s)
//                if (previousTimer >= BossTimings::Death_TotalDuration && bossDef.actionTimer < BossTimings::Death_TotalDuration) {
//                    animParams.SetTrigger(CCL::Utils::HashString("Damage"));
//                }
//
//                // [ 2. 倒れ込み ] : 怯みが終わった瞬間 (2.5s) に発動
//                if (previousTimer >= BossTimings::Death_FlinchTime && bossDef.actionTimer < BossTimings::Death_FlinchTime) {
//                    animParams.SetTrigger(AnimHashes::DEAD_START);
//                }
//
//                // [ 3. 死亡完了 ] : 全アニメーション終了後、地面に横たわるループへ
//                if (bossDef.actionTimer <= 0.0f) {
//                    animParams.SetBool(AnimHashes::IS_DEAD, true);
//
//                    // ※スローモーションを解除する場合はここで行う
//                    // if (_world->HasResource<TimeContext>()) _world->GetResource<TimeContext>().globalScale = 1.0f;
//                }
//            }
//            // ====================================================================
//            // [ 飛び上がりアクション (JumpToHover) ]
//            // アクション開始時に「LevitateStart（浮遊開始）」をトリガーする
//            // ====================================================================
//            else if (bossDef.currentState == BossActionState::JumpToHover) {
//                if (previousTimer >= BossTimings::JumpToHover_Total && bossDef.actionTimer < BossTimings::JumpToHover_Total) {
//                    animParams.SetTrigger(AnimHashes::LEVITATE_START); // ★修正
//                }
//                isAirborne = (bossDef.actionTimer <= BossTimings::JumpToHover_Liftoff);
//            }
//            // ====================================================================
//            // [ 垂直落下アクション (HoverToLand) ]
//            // 落下完了（地面激突）の瞬間に「HeavyLand（重力着地）」をトリガーする
//            // ====================================================================
//            else if (bossDef.currentState == BossActionState::HoverToLand) {
//                isAirborne = (bossDef.actionTimer > BossTimings::HoverToLand_Land);
//
//                // 着地の瞬間のみトリガーを送信（落下開始のトリガーは送らない）
//                if (previousTimer >= BossTimings::HoverToLand_Land && bossDef.actionTimer < BossTimings::HoverToLand_Land) {
//                    animParams.SetTrigger(AnimHashes::HEAVY_LAND); // ★修正
//                }
//            }
//            else if (bossDef.currentState == BossActionState::EscapeJump) {
//                // 1.5s ～ 0.5s の間を空中とする
//                isAirborne = (bossDef.actionTimer <= 1.5f && bossDef.actionTimer > 0.5f);
//
//                // 1.5s の瞬間にジャンプトリガー
//                if (previousTimer >= 1.5f && bossDef.actionTimer < 1.5f) {
//                    animParams.SetTrigger(AnimHashes::JUMP_START);
//                }
//
//                // 0.5s の着地の瞬間に着地トリガー
//                if (previousTimer >= 0.5f && bossDef.actionTimer < 0.5f) {
//                    animParams.SetTrigger(AnimHashes::JUMP_END);
//
//                    // 着地時のカメラシェイクを鳴らしたい場合はここに入れる
//                }
//                }
//                
//
//            // 最後に一括で送信
//            animParams.SetBool(AnimHashes::IS_AIRBORNE, isAirborne);
//            animParams.SetBool(AnimHashes::HASH_IS_BRAKING, isBraking);
//            animParams.SetBool(AnimHashes::IS_STRAFING_RIGHT, isStrafingRightParam);
//            animParams.SetBool(AnimHashes::IS_STRAFING_LEFT, isStrafingLeftParam);
//            animParams.SetBool(AnimHashes::IS_EVADING, isEvadingParam);
//        });
//}
//
//
//// ============================================================================
//// 各アクションの具体的な処理（ヘルパー関数）
//// ============================================================================
//
//void BossActionSystem::AcceptNewCommand(BossActionComponent& bossDef, const BossCommandComponent& cmd, TransformComponent& trans) {
//    // =======================================================
//    // 大技の硬直中、および「怯み(Flinch)中」はAIからの新しい命令を完全に無視する
//    // =======================================================
//    if (bossDef.currentState != BossActionState::None && bossDef.currentState != BossActionState::Move) {
//        return; // 硬直中・怯み中ならAIの命令は届かない（脊髄反射優先）
//    }
//
//    bossDef.currentState = BossActionState::None;
//
//    switch (cmd.currentActionId) {
//
//    case AI::A_MeleeAttack: {
//        bossDef.currentState = BossActionState::Melee;
//        // =======================================================
//        // ★修正: 1.0f ではなく、BossTimings 定数を代入！
//        // =======================================================
//        bossDef.actionTimer = BossTimings::Melee_Duration;
//
//        const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
//        if (playerTrans) {
//            Vector3 dir = playerTrans->position - trans.position;
//            dir.y = 0.0f;
//            if (dir.LengthSquared() > 0.01f) {
//                dir.Normalize();
//                float targetYaw = atan2f(dir.x, dir.z);
//                trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
//                trans.isDirty = true;
//            }
//        }
//        break;
//    }
//    case AI::A_StrafeLeft:
//    case AI::A_StrafeRight:
//    case AI::A_StrafeRandom: {
//        bossDef.currentState = BossActionState::Strafe;
//        bossDef.actionTimer = BossTimings::Strafe_Duration;
//
//        // ★ ランダム性の決定
//        if (cmd.currentActionId == AI::A_StrafeRight) {
//            bossDef.isStrafingRight = true;
//        }
//        else if (cmd.currentActionId == AI::A_StrafeLeft) {
//            bossDef.isStrafingRight = false;
//        }
//        else {
//            // C++の標準的なランダム (0 か 1)
//            bossDef.isStrafingRight = (rand() % 2 == 0);
//        }
//        break;
//    }
//    case AI::A_ChargeAttack: {
//        bossDef.currentState = BossActionState::Charge;
//        bossDef.actionTimer = bossDef.currentChargeTotal;
//        bossDef.jumpRequested = true;
//
//        // ★アーキテクチャ修正: 発動した瞬間のボスの位置を「世界のアンカー」として完全固定！
//        bossDef.chargeAnchorPosition = trans.position;
//
//        // （以下、前回の方向固定ロジック。基準を固定座標に変更してより頑健にする）
//        const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
//        if (playerTrans) {
//            Vector3 targetPos = playerTrans->position;
//            targetPos.y -= 0.5f;
//
//            // ロックされた固定座標からのベクトルを計算
//            Vector3 dir = targetPos - bossDef.chargeAnchorPosition;
//            if (dir.y > 0.0f) dir.y = -0.1f;
//
//            if (dir.LengthSquared() > 0.01f) {
//                dir.Normalize();
//                bossDef.actionDirection = dir;
//            }
//            else {
//                bossDef.actionDirection = Vector3::Transform(Vector3::UnitZ, trans.rotation);
//            }
//        }
//        break;
//    }
//    case AI::A_Move: {
//        bossDef.currentState = BossActionState::Move;
//
//        // ====================================================================
//        // ★究極のハック: Moveに「思考の寿命（タイムアウト）」を持たせる！
//        // 0.5秒間歩いたら一旦 None に戻り、BTに「次は何をする？」と強制的に再評価させる。
//        // ====================================================================
//        bossDef.actionTimer = 0.5f;
//
//        break;
//    }
//    case AI::A_JumpAttack: {
//        bossDef.currentState = BossActionState::JumpAttack;
//        bossDef.actionTimer = BossTimings::JumpAttack_Total; // (ここは既に定数になっていましたわね)
//        // ★修正: ここではまだジャンプフラグを立てない！（タメ中に飛んでしまうため）
//        bossDef.jumpRequested = true; // ← このフラグは ProcessJumpAttack でタイミングを見て消費します
//
//        const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
//        if (playerTrans) {
//            Vector3 dir = playerTrans->position - trans.position;
//            dir.y = 0.0f;
//            float distance = dir.Length();
//
//            if (distance > 0.01f) {
//                dir.Normalize();
//                bossDef.actionDirection = dir;
//                bossDef.dynamicJumpSpeed = distance / 1.5f;
//
//                float targetYaw = atan2f(dir.x, dir.z);
//                trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
//                trans.isDirty = true;
//            }
//        }
//        break;
//    }
//    case AI::A_EscapeToCenter: {
//        bossDef.currentState = BossActionState::EscapeJump;
//        bossDef.actionTimer = BossTimings::EscapeToCenter_Total; // 全体タイム 2.0秒
//        bossDef.jumpRequested = true;
//        bossDef.currentMoveSpeed = 0.0f;
//        break;
//    }
//    case AI::A_EvadeBackward: {
//        bossDef.currentState = BossActionState::Evade;
//        // 新しい Total タイマーを使う
//        bossDef.actionTimer = BossTimings::Evade_Total;
//
//        const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
//        if (playerTrans) {
//            // =========================================================
//            // ★ 逆ベクトルの計算: 「自分の位置 - プレイヤーの位置」
//            // =========================================================
//            Vector3 dir = trans.position - playerTrans->position;
//            dir.y = 0.0f;
//            if (dir.LengthSquared() > 0.01f) {
//                dir.Normalize();
//                bossDef.actionDirection = dir; // 逃げる方向を記憶
//
//                // ※ボスの体（Rotation）はプレイヤーの方を向けたまま（ガン見しながら）後ろに下がる
//                float targetYaw = atan2f(-dir.x, -dir.z);
//                trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
//                trans.isDirty = true;
//            }
//        }
//        break;
//    }
//    case AI::A_PhaseTransition: {
//        bossDef.currentState = BossActionState::PhaseTransition;
//        bossDef.actionTimer = BossTimings::PhaseTransition_Total; // 5.0秒
//        break;
//    }
//    case AI::A_JumpToHover: {
//        bossDef.currentState = BossActionState::JumpToHover;
//        bossDef.actionTimer = BossTimings::JumpToHover_Total;
//        bossDef.jumpRequested = true;
//        break;
//    }
//    case AI::A_HoverToLand: {
//        bossDef.currentState = BossActionState::HoverToLand;
//        bossDef.actionTimer = BossTimings::HoverToLand_Total;
//        break;
//    }
//    default:
//        break;
//    }
//}
//
//
//// ============================================================================
//// 【アクション名】 歩行・追従移動 (Move)
//// 【調整パラメータ (BossActionComponent)】
////  - walkSpeed : プレイヤーへ近づく際の移動速度
////  - turnSpeed : プレイヤーの方向を向く際の旋回速度
//// 【動作仕様】
////   プレイヤーの現在位置へ向かって一定速度で歩行し、同時に滑らかに旋回して向きを合わせる。
//// ============================================================================
//void BossActionSystem::ProcessMove(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, const BossCommandComponent& cmd, float dt, float& outTargetSpeed) {
//    const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
//    if (!playerTrans) return;
//
//    Vector3 moveDir = playerTrans->position - trans.position;
//    moveDir.y = 0.0f;
//
//    if (moveDir.LengthSquared() > 0.01f) {
//        moveDir.Normalize();
//        outTargetSpeed = bossDef.walkSpeed; // ★目標速度(TargetSpeed)を設定
//
//        bossDef.moveDirection = moveDir;
//
//        // 1. 目標の角度 (Target Yaw)
//        float targetYaw = atan2f(moveDir.x, moveDir.z);
//
//        // 2. 現在の角度 (Current Yaw) を取得
//        Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
//        float currentYaw = atan2f(currentForward.x, currentForward.z);
//
//        // 3. 最短の回転方向を計算 (-180度 ～ 180度 に丸める)
//        float diff = WrapAngle(targetYaw - currentYaw);
//
//        // 4. 一定速度で角度を近づける
//        float speed = (bossDef.turnSpeed <= 0.0f) ? 10.0f : bossDef.turnSpeed;
//        float newYaw = currentYaw + diff * (speed * dt);
//
//        // 5. 計算した新しい角度から、純粋なY軸回転のクォータニオンを再構築
//        trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, newYaw);
//        trans.isDirty = true;
//    }
//}
//
//// ============================================================================
//// 【アクション名】 回り込み・横移動 (Circle Strafe)
//// 【動作仕様】
////   プレイヤーへのベクトルと上方向(Y)の外積を利用し、プレイヤーを中心とした
////   円周上を移動（カニ歩き）する。体は常にプレイヤーの方を睨み続ける。
//// ============================================================================
//void BossActionSystem::ProcessStrafe(BossActionComponent& bossDef, TransformComponent& trans, const BossCommandComponent& cmd, float dt, float& outTargetSpeed) {
//    const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
//    if (!playerTrans) return;
//
//    // 1. プレイヤーへのベクトルを出す
//    Vector3 toPlayer = playerTrans->position - trans.position;
//    toPlayer.y = 0.0f;
//
//    if (toPlayer.LengthSquared() > 0.01f) {
//        toPlayer.Normalize();
//
//        // ====================================================================
//        // ★ 外積(Cross Product)による「完璧な横方向」の算出
//        // ====================================================================
//        // プレイヤーへのベクトル と 真上(Y) の外積を取ると、必ず「右」のベクトルが出る
//        Vector3 rightDir = toPlayer.Cross(Vector3::UnitY);
//
//        // 決定されたフラグに従って移動方向を決める
//        bossDef.moveDirection = bossDef.isStrafingRight ? rightDir : -rightDir;
//
//        // ====================================================================
//        // アニメーション(Start/Loop/End)に合わせた速度カーブの適用
//        // ====================================================================
//        if (bossDef.actionTimer > BossTimings::Strafe_LoopStart) {
//            // [Start] 踏み込みのモーション中なので、最高速を出さずタメを作る
//            outTargetSpeed = bossDef.strafeSpeed * 0.5f;
//        }
//        else if (bossDef.actionTimer <= BossTimings::Strafe_EndTrigger) {
//            // [End] ブレーキをかけて立ち止まるモーション中なので、足を止める
//            outTargetSpeed = 0.0f;
//        }
//        else {
//            // [Loop] カニ歩きの巡回速度
//            outTargetSpeed = bossDef.strafeSpeed;
//        }
//
//        // ====================================================================
//        // ★ 移動しながらプレイヤーを睨み続ける（旋回処理）
//        // ====================================================================
//        float targetYaw = atan2f(toPlayer.x, toPlayer.z);
//        Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
//        float currentYaw = atan2f(currentForward.x, currentForward.z);
//
//        float diff = WrapAngle(targetYaw - currentYaw);
//        float speed = (bossDef.turnSpeed <= 0.0f) ? 10.0f : bossDef.turnSpeed;
//        float newYaw = currentYaw + diff * (speed * dt);
//
//        trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, newYaw);
//        trans.isDirty = true;
//    }
//}
//
//// ============================================================================
//// 【アクション名】 突進攻撃 (Charge)
//// 【調整パラメータ (BossActionComponent)】
////  - chargeHoverVelocity : タメ中の浮き上がり初速（高くするほど上空へフワッと浮く）
////  - chargeSpeed         : 突進時の超高速移動スピード
////  - turnSpeed           : タメ中にプレイヤーをロックオンする際の旋回速度
//// 【動作仕様】
////   フェーズ1 (タメ) : その場で上空へ浮き上がりながらプレイヤーの足元をロックオンする。
////   フェーズ2 (突撃) : ロックオンした斜め下のベクトルへ向かって、重力無視で突き刺さる。
////   フェーズ3 (硬直) : ブレーキをかけて立ち止まる。
//// ============================================================================
//void BossActionSystem::ProcessCharge(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, const BossCommandComponent& cmd, float dt, float& outTargetSpeed) {
//
//    // [フェーズ1] 浮遊・タメ (残り時間が Charge_DashStart [1.5s] より大きい期間)
//    if (bossDef.actionTimer > BossTimings::Charge_DashStart) {
//        // ====================================================================
//        // ★修正: 物理的な飛び上がりも、アニメーションの開始(Charge_AnimStart)と完全に同期させる！
//        // 5.5s 〜 2.5s までは地上でじっと睨みつけ、2.5s になった瞬間に飛び上がる。
//        // ====================================================================
//        if (bossDef.jumpRequested && bossDef.actionTimer <= bossDef.currentChargeAnimStart) {
//            moveInput.jumpRequested = true;
//            moveInput.customJumpVelocity = bossDef.chargeHoverVelocity;
//            bossDef.jumpRequested = false;
//        }
//        moveInput.customGravity = 15.0f;
//        outTargetSpeed = 0.0f;
//
//        // ====================================================================
//        // ★ アーキテクチャ修正：ギリギリまでの追跡（Tracking）
//        // ====================================================================
//        // タメている間は、毎フレーム「固定された原点」から「現在のプレイヤー位置」への
//        // ベクトルを再計算し、射線（actionDirection）を更新し続ける。
//        const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
//        if (playerTrans) {
//            Vector3 targetPos = playerTrans->position;
//            targetPos.y -= 0.5f; // 足元狙い
//
//            // ※ bossTrans.position ではなく、固定された chargeAnchorPosition を基準にする！
//            Vector3 dir = targetPos - bossDef.chargeAnchorPosition;
//            if (dir.y > 0.0f) dir.y = -0.1f;
//
//            if (dir.LengthSquared() > 0.01f) {
//                dir.Normalize();
//                bossDef.actionDirection = dir; // 射線を更新！
//                // (※ これにより、DroneFormationSystem側もこの方向を参照するため、通路が自動で回転する)
//            }
//        }
//
//        // ====================================================================
//        // 振り向き（旋回）処理
//        // ====================================================================
//        Vector3 dir = bossDef.actionDirection;
//        float targetYaw = atan2f(dir.x, dir.z);
//        Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
//        float currentYaw = atan2f(currentForward.x, currentForward.z);
//
//        float diff = WrapAngle(targetYaw - currentYaw);
//        float speed = (bossDef.turnSpeed <= 0.0f) ? 10.0f : bossDef.turnSpeed;
//        float newYaw = currentYaw + diff * (speed * dt);
//        trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, newYaw);
//        trans.isDirty = true;
//    }
//    // [フェーズ2] 空中ダッシュ突進 (1.5s ～ 0.5s)
//    else if (bossDef.actionTimer > BossTimings::Charge_BrakeTime) {
//        // 浮遊が終わった瞬間に方向の更新が止まり、記憶した方向へ重力無視で一直線に突進する！
//        moveInput.overrideVerticalVelocity = true; // Y軸も完全に速度で上書き
//
//        // ====================================================================
//        // ★ バックステップ(Evade)の知見を活かした、重みのある突進カーブ
//        // ====================================================================
//        float dashDuration = BossTimings::Charge_DashStart - BossTimings::Charge_BrakeTime;
//        // 進行度 (1.0: 突進開始直後 -> 0.0: ブレーキ寸前)
//        float progress = (bossDef.actionTimer - BossTimings::Charge_BrakeTime) / dashDuration;
//
//        // 突進は完全に停止させず、最高速から70%程度の速度までしか減衰させない（貫通力を残す）
//        // 最初は100%のスピード、後半は70%のスピードになるカーブ
//        float minSpeedRatio = 0.7f;
//        outTargetSpeed = bossDef.chargeSpeed * (minSpeedRatio + (1.0f - minSpeedRatio) * progress);
//
//        // 空間を蹴り飛ばす爆発的な初動（Lerp）
//        float kickPower = 15.0f; // 踏み込みの力（大きいほど瞬時に最高速になる）
//        if (bossDef.currentMoveSpeed < outTargetSpeed) {
//            bossDef.currentMoveSpeed = std::lerp(bossDef.currentMoveSpeed, outTargetSpeed, kickPower * dt);
//        }
//    }
//    // [フェーズ3] ブレーキ・硬直 (0.5s ～ 0.0s)
//    else {
//        outTargetSpeed = 0.0f;
//
//        // ★ ブレーキ時もLerpを使うと「ズザザッ」と滑る重厚な停止になります
//        float brakePower = 8.0f;
//        bossDef.currentMoveSpeed = std::lerp(bossDef.currentMoveSpeed, 0.0f, brakePower * dt);
//    }
//}
//
//// ============================================================================
//// 【アクション名】 ジャンプ攻撃 (Jump Attack)
//// 【調整パラメータ (BossActionComponent)】
////  - jumpAttackApexHeight : ジャンプの頂点の高さ（プレイヤーからどれくらい高く飛ぶか）
////  - jumpAttackGravity    : ジャンプ中の重力（大きいほど素早く落下し、重みが出る）
//// 【動作仕様】
////   滞空時間(Timings)と目標位置から物理の放物線計算（逆算）を用いて「必要な初速」を算出し、
////   確実にプレイヤーの頭上から位置へ着地する。
//// ============================================================================
//void BossActionSystem::ProcessJumpAttack(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, float dt, float& outTargetSpeed) {
//    // [フェーズ1] 地上でのタメ・予兆 (3.0s ～ 2.5s)
//    if (bossDef.actionTimer > BossTimings::JumpAttack_Liftoff) {
//        outTargetSpeed = 0.0f;
//    }
//    // [フェーズ2] 飛翔・空中移動 (2.5s ～ 1.0s)
//    else if (bossDef.actionTimer > BossTimings::JumpAttack_Land) {
//
//        // 滞空時間 (T) = 1.5秒
//        float hangTime = BossTimings::JumpAttack_Liftoff - BossTimings::JumpAttack_Land;
//        float H = bossDef.jumpAttackApexHeight; // 目標とする山の高さ(m)
//
//        // ★ アクションゲーム特有の「高さを指定して重力と初速を逆算する」計算式
//        float requiredGravity = (8.0f * H) / (hangTime * hangTime);
//        float requiredJumpVel = (4.0f * H) / hangTime;
//
//        if (bossDef.jumpRequested) {
//            moveInput.jumpRequested = true;
//            moveInput.customJumpVelocity = requiredJumpVel;
//            bossDef.jumpRequested = false;
//        }
//
//        // 飛んでいる間はずっと専用の重力を物理エンジンに要求する
//        moveInput.customGravity = requiredGravity;
//
//        outTargetSpeed = bossDef.dynamicJumpSpeed;
//        moveInput.desiredVelocity = bossDef.actionDirection * outTargetSpeed;
//    }
//    // [フェーズ3] 着地・硬直 (1.0s ～ 0.0s)
//    else {
//        outTargetSpeed = 0.0f;
//    }
//}
//
//// ============================================================================
//// 【アクション名】 バックステップ回避 (Evade)
//// 【調整パラメータ (BossActionComponent)】
////  - evadeSpeed : 後ろへ飛び退くスピード
//// 【動作仕様】
////   現在向いている方向の「真後ろ（-Z方向）」に対して、重力を無視して素早く後退する。
////   無敵時間や怯み抜けなどと組み合わせて使用される。
//// ============================================================================
//void BossActionSystem::ProcessEvade(BossActionComponent& bossDef, float dt, float& outTargetSpeed) {
//    // Start(タメ)とEnd(着地)の最中は動かず、Loop(空中)の期間だけ物理的に後ろへ下がる
//    if (bossDef.actionTimer <= BossTimings::Evade_Airborne && bossDef.actionTimer > BossTimings::Evade_Land) {
//
//        float airborneDuration = BossTimings::Evade_Airborne - BossTimings::Evade_Land;
//        float progress = (bossDef.actionTimer - BossTimings::Evade_Land) / airborneDuration;
//
//        // 2次関数でカーブを作る (1.0 -> 0.0)
//        outTargetSpeed = bossDef.evadeSpeed * (progress * progress);
//
//        // ====================================================================
//        // ★ 究極の自然な回避カーブ (Lerpによる初速のブレンド)
//        // ====================================================================
//        // いきなりトップギアに入れるとワープして見えてしまうため、
//        // 「現在の速度」と「目標の最高速度」を非常に短い時間（Lerp）で滑らかに繋ぎます。
//        // これにより、地面を「グッ、ダン！」と蹴る重みのある初動が生まれます。
//
//        float kickPower = 15.0f; // 地面を蹴る力（大きいほど素早く最高速に到達する）
//
//        if (bossDef.currentMoveSpeed < outTargetSpeed) {
//            // 現在速度を、最高速に向かって急速に引っ張り上げる
//            bossDef.currentMoveSpeed = std::lerp(bossDef.currentMoveSpeed, outTargetSpeed, kickPower * dt);
//        }
//
//        bossDef.moveDirection = bossDef.actionDirection;
//    }
//    else {
//        outTargetSpeed = 0.0f; // 地面に足がついている時は急ブレーキ
//        bossDef.moveDirection = bossDef.actionDirection;
//    }
//}
//
//// ============================================================================
//// 【アクション名】 待機・注視 (Idle)
//// 【調整パラメータ (BossActionComponent)】
////  - turnSpeed : その場でプレイヤーを目で追う（旋回する）スピード
//// 【動作仕様】
////   移動速度はゼロのまま、プレイヤーの方向を滑らかに向き続ける。
//// ============================================================================
//void BossActionSystem::ProcessIdle(BossActionComponent& bossDef, TransformComponent& trans, const BossCommandComponent& cmd, float dt) {
//    const auto* playerTrans = _world->GetComponent<TransformComponent>(cmd.targetPlayerId);
//    if (!playerTrans) return;
//
//    Vector3 dirToPlayer = playerTrans->position - trans.position;
//    dirToPlayer.y = 0.0f; // 水平方向のみで計算
//
//    // プレイヤーと完全に重なっていない場合のみ計算
//    if (dirToPlayer.LengthSquared() > 0.01f) {
//
//        // 1. プレイヤーの方向の角度 (Target Yaw) を計算
//        float targetYaw = atan2f(dirToPlayer.x, dirToPlayer.z);
//
//        // 2. 現在の自分の角度 (Current Yaw) を取得
//        Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
//        float currentYaw = atan2f(currentForward.x, currentForward.z);
//
//        // 3. 最短の回転方向を計算 (-180度 ～ 180度 に丸める)
//        float diff = WrapAngle(targetYaw - currentYaw);
//
//        // ====================================================================
//        // ★演出の極意：待機中の旋回は、歩行中(turnSpeed)の半分にする
//        // すぐにビュッと向くのではなく、重たい体でジリジリと獲物を睨みつける演出
//        // ====================================================================
//        float idleTurnSpeed = (bossDef.turnSpeed <= 0.0f) ? 10.0f : (bossDef.turnSpeed * 0.5f);
//
//        // 4. 一定速度で角度を近づける
//        float newYaw = currentYaw + diff * (idleTurnSpeed * dt);
//
//        // 5. 新しい角度をクォータニオンとして保存
//        trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, newYaw);
//        trans.isDirty = true;
//    }
//}
//
//// AI/BehaviorTree/BossActionSystem.cpp
//
//// ============================================================================
//// 【アクション名】 第2形態移行演出 (Phase Transition: 計8.0秒の黄金シーケンス)
//// ============================================================================
//void BossActionSystem::ProcessPhaseTransition(CCL::ECS::EntityID bossId, BossActionComponent& bossDef,
//    CharacterMovementInputComponent& moveInput, TransformComponent& trans,
//    BehaviorTreeComponent& btComp, float dt, float& outTargetSpeed)
//{
//    // [初期化処理]
//    if (bossDef.actionTimer == -1.0f) {
//        bossDef.actionTimer = 8.0f;
//        if (auto* animParams = _world->GetComponent<AnimParametersComponent>(bossId)) {
//            animParams->SetTrigger(CCL::Utils::HashString("Damage"));
//        }
//        bossDef.isHovering = true;
//        CCL_LOG_INFO(LogCategory::Game, "[Phase2] 8.0s : Boss Hit! Starting Hit-Stop & Slow-Motion.");
//        if (auto* cmdComp = _world->GetComponent<BossCommandComponent>(bossId)) {
//            cmdComp->requestFormation = DroneFormationType::HighOrbit;
//        }
//    }
//
//    float currentT = bossDef.actionTimer;
//    float nextT = bossDef.actionTimer - dt;
//
//    Vector3 centerAirPos = Vector3(0.0f, 20.0f, 0.0f);
//    outTargetSpeed = 0.0f;
//
//    // ====================================================================
//    // 1. 【状態】毎フレームの物理・移動シミュレーション（if - else if）
//    // ====================================================================
//    if (currentT > 7.2f) {
//        // [ 8.0s 〜 7.2s ] : 被弾 ＆ スローモーション（その場待機）
//        moveInput.overrideVerticalVelocity = true;
//        moveInput.desiredVelocity = Vector3::Zero;
//    }
//    else if (currentT > 6.2f) {
//        // [ 7.2s 〜 6.2s ] : 中央上空への大移動
//        if (auto* bossCmd = _world->GetComponent<BossCommandComponent>(bossId)) {
//			bossCmd->requestFormation = DroneFormationType::HighOrbit; // ドローンを一旦消す指示（安全策）
//        }
//
//        moveInput.overrideVerticalVelocity = true;
//        Vector3 toTarget = centerAirPos - trans.position;
//        float remainingAscent = currentT - 6.2f;
//        if (remainingAscent > 0.01f) {
//            moveInput.desiredVelocity = toTarget / remainingAscent;
//        }
//    }
//    else if (currentT > 1.5f) {
//        // [ 6.2s 〜 1.5s ] : 中央滞空
//        moveInput.overrideVerticalVelocity = true;
//        moveInput.desiredVelocity = Vector3::Zero;
//    }
//    else if (currentT > 0.5f) {
//        // [ 1.5s 〜 0.5s ] : 落下前タメモーション
//        moveInput.overrideVerticalVelocity = true;
//        moveInput.desiredVelocity = Vector3::Zero;
//    }
//    else if (currentT > 0.0f) {
//        // [ 0.5s 〜 0.0s ] : 地面への垂直急降下
//        moveInput.overrideVerticalVelocity = true;
//        Vector3 groundPos = centerAirPos;
//        groundPos.y = 0.0f;
//        Vector3 toTarget = groundPos - trans.position;
//        float remainingDescent = currentT;
//        if (remainingDescent > 0.01f) {
//            moveInput.desiredVelocity = toTarget / remainingDescent;
//        }
//    }
//    else {
//        // [ 0.0s 以下 ] : 着地後硬直
//        moveInput.overrideVerticalVelocity = false;
//        moveInput.desiredVelocity = Vector3::Zero;
//    }
//
//    // ====================================================================
//    // 2. 【イベント】時間を跨いだ瞬間のワンショット判定（独立した if 群）
//    // ====================================================================
//
//    // ① [7.2sの瞬間] スロー解除・上昇開始
//    if (currentT > 7.2f && nextT <= 7.2f) {
//        CCL_LOG_INFO(LogCategory::Game, "[Phase2] 7.2s : Ascending to center! PHASE_START");
//        if (auto* animParams = _world->GetComponent<AnimParametersComponent>(bossId)) {
//            animParams->SetTrigger(AnimHashes::PHASE_START);
//        }
//    }
//
//    // ② [6.2sの瞬間] 中央到達・ドローン生成
//    if (currentT > 6.2f && nextT <= 6.2f) {
//        CCL_LOG_INFO(LogCategory::Game, "[Phase2] 6.2s : Spawning 128 Drones! PHASE_RECALL");
//        if (auto* animParams = _world->GetComponent<AnimParametersComponent>(bossId)) {
//            animParams->SetTrigger(AnimHashes::PHASE_RECALL);
//        }
//        if (auto* cmdComp = _world->GetComponent<BossCommandComponent>(bossId)) {
//            cmdComp->requestFormation = DroneFormationType::HighOrbit;
//        }
//
//        int targetDroneCount = 128;
//        int currentCount = 0;
//        for (auto e : _world->View<DroneComponent>()) currentCount++;
//
//        int spawnCount = targetDroneCount - currentCount;
//        if (spawnCount > 0) {
//            Vector3 spawnPos = centerAirPos;
//            spawnPos.y += 30.0f; // ボスのさらに上空から湧き出させる
//
//            if (_world->HasResource<PrefabSpawnQueue>()) {
//                auto& queue = _world->GetResource<PrefabSpawnQueue>();
//                std::lock_guard<std::mutex> lock(queue.mtx);
//                for (int i = 0; i < spawnCount; ++i) {
//                    queue.commands.push_back({ "Data/Prefabs/Enemy/Drone.json", spawnPos, bossId, DroneFormationType::HighOrbit });
//                }
//            }
//        }
//    }
//
//    // ③ [2.0sの瞬間] ドローン降下指示
//    if (currentT > 2.0f && nextT <= 2.0f) {
//        CCL_LOG_INFO(LogCategory::Game, "[Phase2] 2.0s : Drones descending.");
//        if (auto* cmdComp = _world->GetComponent<BossCommandComponent>(bossId)) {
//            cmdComp->requestFormation = DroneFormationType::OrbitCircle;
//        }
//    }
//
//    // ④ [1.5sの瞬間] 落下前ポーズ
//    if (currentT > 1.5f && nextT <= 1.5f) {
//        CCL_LOG_INFO(LogCategory::Game, "[Phase2] 1.5s : Prepare for impact, PHASE_FALL");
//        if (auto* animParams = _world->GetComponent<AnimParametersComponent>(bossId)) {
//            animParams->SetTrigger(AnimHashes::PHASE_FALL);
//        }
//    }
//
//    // ⑤ [0.0sの瞬間] 着地完了 ＆ 第2形態の脳(BT)へ換装
//    if (currentT > 0.0f && nextT <= 0.0f) {
//        CCL_LOG_INFO(LogCategory::Game, "[Phase2] 0.0s : BOOM! Boss landed.");
//
//        if (auto* animParams = _world->GetComponent<AnimParametersComponent>(bossId)) {
//            animParams->SetTrigger(AnimHashes::PHASE_END);
//        }
//        bossDef.isHovering = false;
//
//        // ボスの基礎ステータスを永続バフ
//        bossDef.chargeSpeed *= 1.8f;
//        bossDef.chargeHoverVelocity *= 1.5f;
//        bossDef.turnSpeed *= 1.5f;
//        bossDef.walkSpeed *= 1.3f;
//        bossDef.evadeSpeed *= 1.2f;
//
//        // ================================================================
//        // チャージ攻撃の予備動作を第2形態用の「5.5秒」に延長する！
//        // ================================================================
//        bossDef.currentChargeTotal = 5.5f;
//        bossDef.currentChargeAnimStart = 4.5f; // 突進1.5sの3秒前に飛び上がる
//
//        // 脳のすげ替え
//        btComp.blackboard.currentPhase = 2;
//        btComp.assetPath = btComp.phase2AssetPath;
//
//        std::fill(btComp.runningNodes.begin(), btComp.runningNodes.end(), -1);
//        btComp.runningNodeId = 0xFFFF;
//        btComp.previousRunningNodeId = 0xFFFF;
//
//        bossDef.currentState = BossActionState::None;
//        bossDef.actionTimer = -1.0f;
//    }
//}
//
//// ============================================================================
//// 【アクション名】 ジャンプ＆滞空 (Jump To Hover)
//// 【動作仕様】
////   タメ動作の後、アリーナ中央(0,0,0)の上空指定位置へ向かって一直線に飛翔し、
////   到達後は isHovering フラグを立てて重力無視の滞空状態に移行する。
//// ============================================================================
//void BossActionSystem::ProcessJumpToHover(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, float dt, float& outTargetSpeed) {
//    if (bossDef.actionTimer > BossTimings::JumpToHover_Liftoff) {
//        // [地上タメ]
//        outTargetSpeed = 0.0f;
//    }
//    else {
//        // [飛翔・移動]
//        bossDef.isHovering = true; // 離陸した瞬間からホバリング（重力無視）状態へ
//        moveInput.overrideVerticalVelocity = true;
//
//        // 目標座標（原点 X=0, Z=0 の上空）
//        Vector3 targetPos = Vector3(0.0f, bossDef.hoverAltitude, 0.0f);
//        Vector3 toTarget = targetPos - trans.position;
//
//        float remainingTime = bossDef.actionTimer;
//        if (remainingTime > 0.01f) {
//            // 残り時間でピタッと目標座標に到達する速度を逆算して直接代入する
//            moveInput.desiredVelocity = toTarget / remainingTime;
//        }
//        else {
//            moveInput.desiredVelocity = Vector3::Zero;
//        }
//        outTargetSpeed = 0.0f;
//    }
//}
//
//// ============================================================================
//// 【アクション名】 降下＆着地 (Hover To Land)
//// 【動作仕様】
////   滞空状態から重力無視を解除し、現在の座標を維持したまま、真下の地面へ垂直急降下。
////   着地と同時に強烈なドスーンという硬直モーションを入れる。
//// ============================================================================
//void BossActionSystem::ProcessHoverToLand(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, const BossCommandComponent& cmd, float dt, float& outTargetSpeed) {
//    if (bossDef.actionTimer > BossTimings::HoverToLand_Land) {
//        // [急降下フェーズ]
//        bossDef.isHovering = false; // 降下開始と同時にフラグを折るが、動き自体は自前で制御
//        moveInput.overrideVerticalVelocity = true; // 急降下のためY軸も速度で上書き
//
//        // ====================================================================
//        // ★ バグ修正＆設計変更：プレイヤーの追従を全削除し「垂直落下」にする
//        // ====================================================================
//        // XZ座標は「現在のボスの位置」をそのままロックし、Y座標だけを0.0f（地面）にする
//        Vector3 targetPos = trans.position;
//        targetPos.y = 0.0f;
//
//        // ターゲット位置（真下の地面）への純粋な垂直ベクトルを算出
//        Vector3 toTarget = targetPos - trans.position;
//        float remainingTime = bossDef.actionTimer - BossTimings::HoverToLand_Land;
//
//        if (remainingTime > 0.01f) {
//            // 残り時間で地面にピッタリ到達する速度を逆算（XZ速度は自動的に0になる）
//            moveInput.desiredVelocity = toTarget / remainingTime;
//        }
//        else {
//            moveInput.desiredVelocity = Vector3::Zero;
//        }
//        outTargetSpeed = 0.0f;
//    }
//    else {
//        // [着地・硬直フェーズ]
//        moveInput.overrideVerticalVelocity = false; // 物理エンジンに重力をお返しする
//        outTargetSpeed = 0.0f;
//    }
//}
//
//// AI/BehaviorTree/BossActionSystem.cpp : ファイルの一番下に追加
//
//// ============================================================================
//// 【アクション名】 中央への脱出跳躍 (Escape Jump)
//// 【動作仕様】
////   2.0s - 1.5s : 膝を曲げて力を溜めるタメ動作
////   1.5s - 0.5s : 空中を移動し、0.5sジャストで原点(0,0,0)上空へ到着する
////   0.5s - 0.0s : 着地硬直
//// ============================================================================
//void BossActionSystem::ProcessEscapeJump(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, float dt, float& outTargetSpeed) {
//
//    float liftoffTime = 1.5f;
//    float landTime = 0.5f;
//
//    if (bossDef.actionTimer > liftoffTime) {
//        // [フェーズ1: タメ]
//        outTargetSpeed = 0.0f;
//
//        // 中央(0,0,0)を向く旋回処理
//        Vector3 toCenter = Vector3::Zero - trans.position;
//        toCenter.y = 0.0f;
//        if (toCenter.LengthSquared() > 0.01f) {
//            toCenter.Normalize();
//            float targetYaw = atan2f(toCenter.x, toCenter.z);
//            trans.rotation = Quaternion::Slerp(trans.rotation, Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw), 10.0f * dt);
//            trans.isDirty = true;
//        }
//    }
//    else if (bossDef.actionTimer > landTime) {
//        // [フェーズ2: 空中大跳躍]
//        float hangTime = liftoffTime - landTime; // 1.0秒
//        float apexHeight = 12.0f; // ジャンプの高さ
//
//        // 必要な重力と初速を逆算
//        float requiredGravity = (8.0f * apexHeight) / (hangTime * hangTime);
//        float requiredJumpVel = (4.0f * apexHeight) / hangTime;
//
//        if (bossDef.jumpRequested) {
//            moveInput.jumpRequested = true;
//            moveInput.customJumpVelocity = requiredJumpVel;
//            bossDef.jumpRequested = false;
//        }
//
//        moveInput.customGravity = requiredGravity;
//        moveInput.overrideVerticalVelocity = false; // Y軸は重力に任せる
//
//        // 確実な原点収束アルゴリズム (XZ軸の完全補間)
//        Vector3 currentXZ = trans.position;
//        currentXZ.y = 0.0f;
//        Vector3 targetXZ = Vector3::Zero;
//
//        Vector3 toTargetXZ = targetXZ - currentXZ;
//        float remainingHangTime = bossDef.actionTimer - landTime;
//
//        if (remainingHangTime > 0.01f) {
//            // 残り時間で割り、着地の瞬間にピッタリ (0,0) に届く速度を出し続ける
//            moveInput.desiredVelocity = toTargetXZ / remainingHangTime;
//            moveInput.desiredVelocity.y = 0.0f; // Y軸速度は上書きしない
//        }
//
//        outTargetSpeed = 0.0f;
//    }
//    else {
//        // [フェーズ3: 着地硬直]
//        outTargetSpeed = 0.0f;
//        moveInput.desiredVelocity = Vector3::Zero;
//    }
//}
//
//
//REGISTER_LOGIC_SYSTEM(BossActionSystem, Priority::LogicStage::L04_Physics);