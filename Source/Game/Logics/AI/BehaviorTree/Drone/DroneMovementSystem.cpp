///**
// * @file DroneMovementSystem.cpp
// * @brief ドローン移動システムの実装
// */
//#include "DroneMovementSystem.h"
//#include "Game/Core/SystemPriority.h"
//#include "ECS/System/CCL_SystemRegistry.h"
//#include <cmath>
//#include "Engine/Physics/IPhysicsAPI.h"
//
//using namespace CCL::ECS;
//using namespace DirectX::SimpleMath;
//
//void DroneMovementSystem::Update(float rawDt) {
//    // 1. Facade(IPhysicsAPI)がResourceに登録されているかチェック
//    if (!_world->HasResource<std::shared_ptr<IPhysicsAPI>>()) return;
//
//    // shared_ptrを取り出してローカルに保持
//    auto physicsAPI = _world->GetResource<std::shared_ptr<IPhysicsAPI>>();
//
//    m_currentTime += rawDt; // ★追加: システム時間を進める
//
//
//    // ★ ラムダの引数に const TimeState& time を追加
//    ForEachWithIDParallel([this, physicsAPI, rawDt](EntityID id, TransformComponent& trans, DroneComponent& drone, const TimeState& time) {
//
//        // ★ ドローン各自の時計を使用（ドローン自身が殴られた時などにピタッと止まる）
//        float dt = time.localDt;
//
//        // ====================================================================
//        // ★ 追加: 【数学的ロールバック】
//        // 前フレームで足された「揺れ」を一度引き算し、本来のクリーンな陣形座標に戻す
//        // ====================================================================
//        trans.position = trans.position - drone.currentShakeOffset;
//        drone.currentShakeOffset = DirectX::SimpleMath::Vector3::Zero;
//
//        // ====================================================================
//        // ★ 究極のフェイルセーフ：「リード（Leash）」によるスタック救済
//        // ====================================================================
//        if (const auto* bossTrans = _world->GetComponent<TransformComponent>(drone.ownerBossId)) {
//
//            // ボスとドローンの距離の2乗を計算
//            float distToBossSqr = (trans.position - bossTrans->position).LengthSquared();
//            float leashRadius = 140.0f; // ★ 許容する最大距離
//
//            // ドローンがアリーナの外や遥か彼方に吹っ飛んでいた場合
//            if (distToBossSqr > (leashRadius * leashRadius)) {
//                // ボスの頭上（または背後）の安全圏へ強制ワープさせる
//                trans.position = bossTrans->position + DirectX::SimpleMath::Vector3(0.0f, 15.0f, 0.0f);
//
//                // 速度と状態をリセットし、陣形構築へ強制復帰
//                drone.currentVelocity = DirectX::SimpleMath::Vector3::Zero;
//                drone.currentState = DroneState::MoveToTarget;
//                trans.isDirty = true;
//                trans.isTeleported = true;
//
//                // ===================================================================
//                // ★ 修正：物理エンジン側の剛体（肉体）も強制ワープさせる！
//                // 速度と回転はループの最後で set されるため、ここでは Position だけでOK
//                // ===================================================================
//                physicsAPI->SetPosition(id, DirectX::XMFLOAT3(trans.position.x, trans.position.y, trans.position.z));
//            }
//        }
//
//        drone.hoverTimeOffset += dt;
//
//        switch (drone.currentState) {
//        case DroneState::MoveToTarget: ProcessMoveToTarget(trans, drone, dt); break;
//        case DroneState::FireCharge:   ProcessFireCharge(trans, drone, dt); break;
//        case DroneState::Idle:         ProcessIdle(trans, drone, dt); break;
//        case DroneState::LockOn:       ProcessLockOn(trans, drone, dt); break;
//        case DroneState::Hold:         ProcessHold(trans, drone, dt); break; 
//        case DroneState::HorizontalCharge: ProcessHorizontalCharge(trans, drone, dt); break;
//        case DroneState::Shaking:      ProcessShaking(trans, drone, dt, m_currentTime); break;
//        }
//
//        // =================================================================
//        // ★ 4. Facadeを経由した物理の注入 (依存性逆転の実現)
//        // AIはJoltを知らない。ただ「APIに速度と回転を渡す」だけ。
//        // =================================================================
//
//        // ★追加：Joltに渡す直前に、蓄積した微小な計算誤差（ドリフト）を浄化する
//        DirectX::XMVECTOR quatVec = DirectX::XMLoadFloat4(&trans.rotation);
//
//        // 正規化を実行（これで長さがピッタリ 1.0 に浄化される）
//        quatVec = DirectX::XMQuaternionNormalize(quatVec);
//
//        // 正規化した結果を trans.rotation に書き戻す
//        DirectX::XMStoreFloat4(&trans.rotation, quatVec);
//
//        physicsAPI->SetRotation(id, DirectX::XMFLOAT4(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w));
//
//        // -------------------------------------------------------------
//        // ★ 最終的な物理速度の決定
//        // -------------------------------------------------------------
//        DirectX::SimpleMath::Vector3 finalVel = drone.currentVelocity;
//
//        // 【修正】物理速度のタイムスケール・スケーリング
//        // 自分の時計(dt)が、現実時間(rawDt)に対してどれくらい遅れているかの比率を出す
//        // 例: 完全に止まっているなら 0.0倍、スローなら 0.1倍、通常なら 1.0倍
//        float timeScale = (rawDt > 0.0001f) ? (dt / rawDt) : 0.0f;
//        
//        // 物理エンジンに渡す速度自体を、タイムスケールに合わせて割引きする
//        finalVel *= timeScale;
//
//        physicsAPI->SetLinearVelocity(id, finalVel);
//        
//
//        });
//}
//
//
//// ============================================================================
//// 各ステートの具体的な物理演算ロジック
//// ============================================================================
//
//inline void DroneMovementSystem::ProcessMoveToTarget(TransformComponent& trans, DroneComponent& drone, float dt) {
//    float pGain = 20.0f;
//    float damping = 15.0f;
//    Vector3 displacement = drone.targetPosition - trans.position;
//    Vector3 targetVelocity = displacement * pGain;
//
//    float maxSpeed = drone.moveSpeed * 2.5f;
//    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed) {
//        targetVelocity.Normalize();
//        targetVelocity *= maxSpeed;
//    }
//
//    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity, (std::min)(damping * dt, 1.0f));
//
//    // ====================================================================
//    // ★ 回転バグ修正: 3D対応＆「-Z前方」対応の完全な回転ロジック
//    // ====================================================================
//    if (drone.targetLookDirection.LengthSquared() > 0.001f) {
//        Vector3 lookDir = drone.targetLookDirection;
//        lookDir.Normalize();
//
//        // エンジンの前方向(-Z)を0度としてYawを計算
//        float targetYaw = atan2f(lookDir.x, -lookDir.z);
//        // 上下の首振り(Pitch)も計算 (NaN防止のため-1.0〜1.0にクランプ)
//        float targetPitch = asinf(std::clamp(lookDir.y, -1.0f, 1.0f));
//
//        // 3D空間の完全な回転行列を生成
//        Quaternion targetRot = Quaternion::CreateFromYawPitchRoll(targetYaw, targetPitch, 0.0f);
//        trans.rotation = Quaternion::Slerp(trans.rotation, targetRot, (std::min)(25.0f * dt, 1.0f));
//        trans.isDirty = true;
//    }
//}
//
//// ============================================================================
//// 超高速突撃・およびその予備動作 (FireCharge)
//// ============================================================================
//inline void DroneMovementSystem::ProcessFireCharge(TransformComponent& trans, DroneComponent& drone, float dt) {
//    if (drone.fireDirection.LengthSquared() > 0.001f) {
//
//        // ====================================================================
//        // [フェーズ1] アンティシペーション（予備動作・タメ）
//        // ====================================================================
//        if (drone.stateTimer > 0.0f) {
//            drone.stateTimer -= dt; // タイマーを消費
//
//            // -------------------------------------------------------------
//            // 【調整パラメーター】予備動作（タメ）のルック＆フィール
//            // -------------------------------------------------------------
//            float drawbackSpeed = 2.0f;       // ① 後ろに引き絞るスピード
//            float upwardLiftSpeed = 4.0f;     // ② ★追加: フワッと上に浮き上がるスピード
//            float jitterAmplitude = 5.5f;     // ③ ★修正: 震えの大きさ（元の1.5fから倍増）
//            float jitterSpeed = 100.0f;        // ④ ★追加: 震えの速さ
//            // -------------------------------------------------------------
//
//            // 突撃方向と逆へ下がりつつ、少し上(Y軸)に浮き上がる
//            drone.currentVelocity = (-drone.fireDirection * drawbackSpeed) + (Vector3::UnitY * upwardLiftSpeed);
//
//            // 力を溜めている威圧感を出すための、激しいブルブル震え（Jitter）
//            drone.currentVelocity.x += std::cos(drone.hoverTimeOffset * jitterSpeed) * jitterAmplitude;
//            drone.currentVelocity.y += std::sin(drone.hoverTimeOffset * (jitterSpeed * 1.2f)) * jitterAmplitude;
//            drone.currentVelocity.z += std::cos(drone.hoverTimeOffset * (jitterSpeed * 0.8f)) * jitterAmplitude;
//
//            // 下がっている間も、回転はプレイヤー（進行方向）をガン見し続ける
//            float targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
//            Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
//            trans.rotation = Quaternion::Slerp(trans.rotation, targetRot, 20.0f * dt);
//            trans.isDirty = true;
//        }
//        // ====================================================================
//        // [フェーズ2] 超高速突撃（リリース）
//        // ====================================================================
//        else {
//            // -------------------------------------------------------------
//            // 【調整パラメーター】リリース（射出）時のルック＆フィール
//            // -------------------------------------------------------------
//            float chargeSpeedMultiplier = 2.5f; // ⑤ 射出時のスピード倍率
//            // -------------------------------------------------------------
//
//            // タイマーが切れた瞬間、溜めた力を解放して一気に前進！
//            drone.currentVelocity = drone.fireDirection * (drone.moveSpeed * chargeSpeedMultiplier);
//
//            float targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
//            Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
//            trans.rotation = Quaternion::Slerp(trans.rotation, targetRot, 25.0f * dt);
//            trans.isDirty = true;
//        }
//    }
//    else {
//        // フェイルセーフ
//        drone.currentVelocity = Vector3::Zero;
//    }
//}
//
//// ============================================================================
//// ProcessIdle の究極シンプル版（絶対に爆発しない・震えない）
//// ============================================================================
//inline void DroneMovementSystem::ProcessIdle(TransformComponent& trans, DroneComponent& drone, float dt) {
//
//    // 1. 目標までのベクトルを計算
//    Vector3 displacement = drone.targetPosition - trans.position;
//
//    // ====================================================================
//    // ★ 追従の機敏化：フワフワ感は残しつつ、置いていかれないようにする
//    // ====================================================================
//    float pGain = 10.0f;    // 以前(4.0f)の2.5倍
//    float damping = 8.0f;   // 以前(5.0f)の1.6倍
//
//    Vector3 targetVelocity = displacement * pGain;
//
//    // ★ デスリング等でプレイヤーに追従するため、Idle時でも基本スピードの 1.5倍 まで許可する
//    float maxSpeed = drone.moveSpeed * 1.5f;
//    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed) {
//        targetVelocity.Normalize();
//        targetVelocity *= maxSpeed;
//    }
//
//    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity, damping * dt);
//
//    // 陣形時の「フワフワ感（ホバー）」
//    float hoverFrequency = 1.0f;
//    float hoverAmplitude = 3.0f;
//    drone.currentVelocity.y += std::cos(drone.hoverTimeOffset * hoverFrequency) * hoverAmplitude * dt;
//
//    // 回転処理：振り向き速度を 10.0f から 15.0f へ少し強化
//    if (drone.targetLookDirection.LengthSquared() > 0.001f) {
//        float targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
//        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
//        trans.rotation = Quaternion::Slerp(trans.rotation, targetRot, 15.0f * dt);
//        trans.isDirty = true;
//    }
//}
//
//// ============================================================================
//// ★追加：発射前のエネルギー充填（ブルブル震える演出）
//// ============================================================================
//inline void DroneMovementSystem::ProcessLockOn(TransformComponent& trans, DroneComponent& drone, float dt) {
//    float pGain = 15.0f;
//    float damping = 10.0f;
//
//    Vector3 displacement = drone.targetPosition - trans.position;
//    Vector3 targetVelocity = displacement * pGain;
//
//
//    // 速度上限
//    float maxSpeed = drone.moveSpeed * 1.5f;
//    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed) {
//        targetVelocity.Normalize();
//        targetVelocity *= maxSpeed;
//    }
//
//    // 発射直前のエネルギー充填を表現する、激しい微振動（Jitter）
//    float jitterX = std::cos(drone.hoverTimeOffset * 50.0f) * 2.0f;
//    float jitterY = std::sin(drone.hoverTimeOffset * 60.0f) * 2.0f;
//    float jitterZ = std::cos(drone.hoverTimeOffset * 40.0f) * 2.0f;
//
//    targetVelocity.x += jitterX;
//    targetVelocity.y += jitterY;
//    targetVelocity.z += jitterZ;
//
//    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity, damping * dt);
//
//    // 3. 回転処理（脳が指示した方向へ首を振る）
//    if (drone.targetLookDirection.LengthSquared() > 0.001f) {
//        float targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
//        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
//
//        // ※ 10.0f の回転スピード部分は、元の関数に書かれていた数値（10.0f, 20.0f, 25.0fなど）をそのまま使ってください
//        trans.rotation = Quaternion::Slerp(trans.rotation, targetRot, 10.0f * dt);
//        trans.isDirty = true;
//    }
//}
//
//
//// ============================================================================
//// ★修正：完全停止（急減速からのイーズアウト停止）
//// ============================================================================
//inline void DroneMovementSystem::ProcessHold(TransformComponent& trans, DroneComponent& drone, float dt) {
//
//    // 【調整パラメーター】 急ブレーキの摩擦力
//    // 以前の 20.0f（壁激突）から、5.0f（急ブレーキ）に引き下げました。
//    // 数値を下げるほど、ツルッと氷の上を滑るように止まるようになります。
//    float damping = 3.0f;
//
//    // 現在の速度を、ゼロに向かって滑らかに減衰させる（徐々にブレーキが弱まる美しい減速曲線）
//    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, Vector3::Zero, damping * dt);
//
//    // ★重要: Lerpの計算上、永遠に完全なゼロにはならないため、
//    // 「ほぼ止まった」と見なせる微小な速度になった瞬間に、速度をピタッと完全に0（空間固定）にする。
//    if (drone.currentVelocity.LengthSquared() < 0.01f) {
//        drone.currentVelocity = Vector3::Zero;
//    }
//
//    // 回転（向いている方向）はそのまま維持する
//
//    // ====================================================================
//    // ★ 究極のバグ修正: 描画の強制更新フラグ
//    // 透明な物理肉体がブレーキをかけて滑っている間、3Dモデルのガワも
//    // ちゃんとそれに追従するように毎フレーム描画を更新させます。
//    // ====================================================================
//    trans.isDirty = true;
//}
//
//
//// ============================================================================
//// 水平突撃・およびその予備動作 (HorizontalCharge)
//// ============================================================================
//inline void DroneMovementSystem::ProcessHorizontalCharge(TransformComponent& trans, DroneComponent& drone, float dt) {
//    if (drone.fireDirection.LengthSquared() > 0.001f) {
//
//        // ====================================================================
//        // [フェーズ1] アンティシペーション（そのままの位置から少し後ろに引く＆震える）
//        // ====================================================================
//        if (drone.stateTimer > 0.0f) {
//            drone.stateTimer -= dt;
//            float drawbackSpeed = 4.0f;
//
//            // ★ ユーザー要望：突撃前に震える予備動作（Shake）を追加！
//            float jitterSpeed = 100.0f;
//            float jitterAmplitude = 4.5f; // ブルブル震える強さ
//
//            Vector3 baseVelocity = -drone.fireDirection * drawbackSpeed;
//
//            // Jitter（ノイズ）加算
//            baseVelocity.x += std::cos(drone.hoverTimeOffset * jitterSpeed) * jitterAmplitude;
//            baseVelocity.y += std::sin(drone.hoverTimeOffset * (jitterSpeed * 1.2f)) * jitterAmplitude;
//            baseVelocity.z += std::cos(drone.hoverTimeOffset * (jitterSpeed * 0.8f)) * jitterAmplitude;
//
//            // 上には浮かず、発射方向の「真逆」へ真っ直ぐ下がりつつ震える
//            drone.currentVelocity = baseVelocity;
//
//            if (drone.targetLookDirection.LengthSquared() > 0.001f) {
//                Vector3 lookDir = drone.targetLookDirection;
//                lookDir.Normalize();
//                float targetYaw = atan2f(lookDir.x, -lookDir.z);
//                float targetPitch = asinf(std::clamp(lookDir.y, -1.0f, 1.0f));
//                Quaternion targetRot = Quaternion::CreateFromYawPitchRoll(targetYaw, targetPitch, 0.0f);
//                trans.rotation = Quaternion::Slerp(trans.rotation, targetRot, 20.0f * dt);
//                trans.isDirty = true;
//            }
//        }
//        // ====================================================================
//        // [フェーズ2] 超高速突撃（リリース）
//        // ====================================================================
//        else {
//            float chargeSpeedMultiplier = 2.0f; // 射出時のスピード倍率
//            drone.currentVelocity = drone.fireDirection * (drone.moveSpeed * chargeSpeedMultiplier);
//
//            float targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
//            Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
//            trans.rotation = Quaternion::Slerp(trans.rotation, targetRot, 25.0f * dt);
//            trans.isDirty = true;
//        }
//    }
//    else {
//        drone.currentVelocity = Vector3::Zero;
//    }
//}
//
//// ============================================================================
//// 震え (Shaking) : 本来の陣形移動を完全に実行した上で、視覚的ノイズを加算する
//// ============================================================================
//inline void DroneMovementSystem::ProcessShaking(TransformComponent& trans, DroneComponent& drone, float dt, float currentTime) {
//
//    // ========================================================================
//    // ★ 最大の修正点：自前で移動を書くのをやめ、既存の関数を再利用する！
//    // これにより「MoveToTarget」と全く同じ完璧な陣形移動が100%保証されます。
//    // ========================================================================
//    ProcessMoveToTarget(trans, drone, dt);
//
//    // ------------------------------------------------------------------------
//        // ★ 調整パラメーター（ゆっくり・大きく震わせる）
//        // ------------------------------------------------------------------------
//        // 1. 振動スピード（周波数）
//        // 修正前: currentTime * 60.0f (超高速なブブブブというブレ)
//        // 修正後: currentTime * 25.0f (ガガガガ、あるいはグワングワンという大きな予兆の揺れ)
//    float t = currentTime * 60.0f;
//
//    // 2. 振幅（ブレの最大半径：メートル単位）
//    // 修正前: 1.2f という係数が offsets の計算の末尾に乗算されていました。
//    // 修正後: 明確に 0.8f (最大80cm) や 1.2f (最大1.2m) など、見た目のブレ幅を設定します。
//    float shakeAmp = 0.8f; // ★まずは 0.8f (80cm) あたりからテストし、物足りなければ 1.5f などに盛ってください
//
//    // 個体インデックスを利用してバラバラの方向に震わせる
//    float offsetX = sinf(t + drone.localIndex * 1.3f) * shakeAmp;
//    float offsetY = cosf(t * 1.1f + drone.localIndex * 0.7f) * shakeAmp;
//    float offsetZ = sinf(t * 1.3f + drone.localIndex * 2.1f) * shakeAmp;
//
//    // 今回のノイズを記憶し、実際の座標に加算する
//    drone.currentShakeOffset = DirectX::SimpleMath::Vector3(offsetX, offsetY, offsetZ);
//    trans.position = trans.position + drone.currentShakeOffset;
//
//    trans.isDirty = true;
//}
//
//
//// 実際の座標更新を行うため、L04_Physicsを指定
//REGISTER_LOGIC_SYSTEM(DroneMovementSystem, Priority::LogicStage::L04_Physics);