/**
 * @file DroneFormationSystem.cpp
 * @brief ドローンフォーメーションシステムの実装
 */
#include "DroneFormationSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include <cmath>
#include <imgui.h>
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include "Game/Logics/AI/BehaviorTree/BossActionComponent.h"
#include <mutex>
#include "Game/Logics/Character/CharacterMovementInputComponent.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;


namespace {
    // ★ 偏差撃ち（予測射撃）のターゲット座標を算出する関数
    DirectX::SimpleMath::Vector3 CalculatePredictiveTarget(
        const DirectX::SimpleMath::Vector3& dronePos,
        const TransformComponent& playerTrans,
        const CharacterMovementInputComponent& playerMove,
        float bulletSpeed)
    {
        using namespace DirectX::SimpleMath;

        Vector3 playerPos = playerTrans.position;
        playerPos.y += 1.0f; // プレイヤーの胴体（重心）を狙う

        // プレイヤーが現在入力している移動ベクトル（速度）
        Vector3 playerVel = playerMove.desiredVelocity;

        // ドローンからプレイヤーまでの距離
        float distance = (playerPos - dronePos).Length();

        // 弾が着弾するまでの予測時間
        float timeToHit = distance / bulletSpeed;

        // ★ 未来の位置 = 現在地 + (速度 × 到達時間)
        // ※ 1.0 を掛けると「完璧な予測」になり、プレイヤーが急停止すると必ず外れる。
        // ※ 0.8f 程度を掛けることで「少し甘い予測」になり、避け方にバリエーションが生まれる。
        float predictionFactor = 0.8f;

        return playerPos + (playerVel * (timeToHit * predictionFactor));
    }
}

#define MAX_PATH 512

namespace {
    // 仮想リーダーの軌跡を記録する構造体
    struct PathNode {
        DirectX::SimpleMath::Vector3 pos;
        DirectX::SimpleMath::Vector3 dir;
    };

    // 過去の軌跡を最大512フレーム（約8秒分）保存するリングバッファ
    PathNode s_path[MAX_PATH];
    int s_pathHead = 0;
    int s_pathCount = 0;

    DirectX::SimpleMath::Vector3 s_leaderPos;
    DirectX::SimpleMath::Vector3 s_leaderDir;

    std::mutex s_pathMutex; // マルチスレッド保護用
}

// ============================================================================
// メインの更新処理
// ============================================================================
void DroneFormationSystem::Update(float rawDt) {

    // ★ 陣形全体の回転時間は、世界の親時計（TimeContext）に合わせて歪ませる
    float globalScale = 1.0f;
    if (_world->HasResource<TimeContext>()) {
        globalScale = _world->GetResource<TimeContext>().globalScale;
    }
    m_currentTime += rawDt * globalScale;
    float currentTime = m_currentTime;

    // ====================================================================
    // ★ 究極のハック：ドローンの自動ナンバリング（堅牢化版）
    // ====================================================================
    // まず、実際のドローンのエンティティIDをすべてリストアップする
    std::vector<EntityID> activeDrones;
    for (auto e : _world->View<DroneComponent>()) {
        activeDrones.push_back(e);
    }
    int actualDroneCount = static_cast<int>(activeDrones.size());

    if (actualDroneCount > 0) {
        // 先頭のドローンの記憶を確認
        auto* firstDrone = _world->GetComponent<DroneComponent>(activeDrones[0]);

        // ★修正: 総数が違っている場合【だけでなく】、念のため毎フレーム
        // 「インデックスが重複していないか（0番が複数いないか）」の厳密チェックを行うのは重いので、
        // 総数が変わった瞬間に、抽出したリスト(activeDrones)を使って確実にナンバリングする！
        if (firstDrone && firstDrone->totalDrones != actualDroneCount) {

            // リストの順番通りに、0から確実に番号を振り直す
            for (int i = 0; i < actualDroneCount; ++i) {
                auto* d = _world->GetComponent<DroneComponent>(activeDrones[i]);
                if (d) {
                    d->totalDrones = actualDroneCount;
                    d->localIndex = static_cast<uint16_t>(i);
                }
            }
            CCL_LOG_INFO(LogCategory::Game, "Drones re-numbered. Total: %d", actualDroneCount);
        }
    }

    // 超並列処理による各ドローンの更新
    ForEachParallel([this, currentTime](DroneComponent& drone, const TimeState& time) {

        // ★ ドローン各自の時計を使用（突撃待機のタイマー減算などに使う）
        float dt = time.localDt;

        // 1. 親と指示の取得（フェイルセーフ）
        const auto* bossTrans = _world->GetComponent<TransformComponent>(drone.ownerBossId);
        const auto* bossCmd = _world->GetComponent<BossCommandComponent>(drone.ownerBossId);
        if (!bossTrans || !bossCmd) return;

        // =================================================================
        // フェーズ 1: エッジ（瞬間）の処理
        // ボスからの命令が「切り替わった瞬間」のステート初期化を行う
        // =================================================================
        HandleStateTransition(drone, *bossTrans, *bossCmd);

        // =================================================================
        // フェーズ 2: レベル（継続）の処理
        // 現在のステートに基づいて、毎フレーム継続的に目標座標を再計算する
        // =================================================================
        switch (drone.currentFormation) {
            // 円陣を描く動き
        case DroneFormationType::OrbitCircle:
            CalculateOrbitCircle(drone, *bossTrans, currentTime);
            break;

            // 順番にプレイヤーへ突撃する動き
        case DroneFormationType::HighOrbitSnipe:
            CalculateHighOrbitSnipe(drone, *bossTrans, *bossCmd, dt, currentTime);
            break;

            // プレイヤーを囲む「処刑の輪」の動き
        case DroneFormationType::DeathRing:   
            CalculateDeathRing(drone, *bossCmd,dt, currentTime);
            break;

            // ボスの前面に壁を作る「絶対防衛陣形」の動き
        case DroneFormationType::AegisShield: 
            CalculateAegisShield(drone, *bossTrans, *bossCmd);
            break;

        case DroneFormationType::HighOrbit:
            CalculateHighOrbit(drone, *bossTrans, currentTime);
                break;

        case DroneFormationType::LowOrbit:
            CalculateLowOrbit(drone, *bossTrans, currentTime);
                break;

        case DroneFormationType::SpreadLockOn:
            CalculateSpreadLockOn(drone, *bossTrans, currentTime);
                break;
        case DroneFormationType::CloseGuard:
            CalculateCloseGuard(drone, *bossTrans, currentTime);
            break;
        case DroneFormationType::BarrierBurst:
            CalculateBarrierBurst(drone, *bossTrans, dt, currentTime);
            break;

        case DroneFormationType::ChargeTunnel:
            CalculateChargeTunnel(drone, *bossTrans, *bossCmd,dt);
            break;
            // 収納されている状態は目標座標を更新しない（ドローン移動システムが勝手にボスの近くに置いてくれる想定）
        case DroneFormationType::HoldPosition:
            // ★あえて「目標座標を一切更新しない」ことで、
            // ドローンが前回命令された時のワールド座標に永遠に固定され続けます。
            break;
        case DroneFormationType::CycloneBurst: 
            CalculateCycloneBurst(drone, *bossTrans, dt, currentTime);
            break;

            // 第２形態
        case DroneFormationType::HighLowOrbit:
            CalculateHighLowOrbit(drone, *bossTrans, currentTime);
            break;

        case DroneFormationType::CrossHelix:
            CalculateCrossHelix(drone, *bossCmd, currentTime);
            break;

        case DroneFormationType::KamikazeSwarm:
            // 計算ロジックは SequentialAttack と全く同じで使い回せる（タイマーだけ違うため）
            //CalculateSequentialAttack(drone, *bossCmd, dt);
            break;
        case DroneFormationType::HighLowCycloneBurst:
            CalculateHighLowCycloneBurst(drone, *bossTrans, dt, currentTime);
            break;
        case DroneFormationType::SweepingBeam:
            CalculateSweepingBeam(drone, *bossTrans, currentTime);
            break;

        case DroneFormationType::InfinityEight:
            CalculateInfinityEight(drone, *bossTrans, currentTime);
            break;

        case DroneFormationType::SwarmRiver:
            // 濁流は「プレイヤーの方向へ流れる」ため、bossCmd も渡す必要があります
            CalculateSwarmRiver(drone, *bossTrans, *bossCmd, dt, currentTime);
            break;

        case DroneFormationType::InfinitySnipe:
            // ★ 分離された純粋な計算関数を呼ぶだけ
            CalculateInfinitySnipe(drone, *bossTrans, *bossCmd, dt, currentTime);
            break;
        case DroneFormationType::SideSlapWhip:
            CalculateSideSlapWhip(drone, *bossTrans, dt);
            break;
        case DroneFormationType::GuillotineSlam:
            CalculateGuillotineSlam(drone, *bossTrans, dt);
            break;
        case DroneFormationType::ClapSlam:
            CalculateClapSlam(drone, *bossTrans, *bossCmd, dt);
            break;
        case DroneFormationType::TsunamiWave:
            CalculateTsunamiWave(drone, *bossTrans, *bossCmd, currentTime);
            break;
        case DroneFormationType::MegaClapSlam_UpperFirst:
            CalculateMegaClapSlam(drone, *bossTrans, *bossCmd, dt, true);
            break;

        case DroneFormationType::MegaClapSlam_LowerFirst:
            CalculateMegaClapSlam(drone, *bossTrans, *bossCmd, dt, false);
            break;
            // メガ・シールドチャージの計算呼び出し
        case DroneFormationType::MegaShieldCharge_UpperFirst:
            CalculateMegaShieldCharge(drone, *bossTrans, *bossCmd, dt, true);
            break;
        case DroneFormationType::MegaShieldCharge_LowerFirst:
            CalculateMegaShieldCharge(drone, *bossTrans, *bossCmd, dt, false);
            break;
        case DroneFormationType::Hidden:
        default:
            break;
        }
        });
}

// ============================================================================
// ヘルパー関数の実装 (inlineにより関数呼び出しのコストを排除)
// ============================================================================

// ボスの命令が変更された瞬間の状態遷移処理　（たった１フレームだけ、ロックオン状態になるのがポイント！）
inline void DroneFormationSystem::HandleStateTransition(DroneComponent& drone, const TransformComponent& bossTrans, const BossCommandComponent& bossCmd) {
    // 命令が切り替わった瞬間の処理：新しいフォーメーションに応じて状態を初期化する
    if (drone.currentFormation != bossCmd.requestFormation) {

        drone.currentFormation = bossCmd.requestFormation;
        drone.currentVelocity = Vector3::Zero; // ← 追加
        drone.stateTimer = 0.0f; // ← 追加（個別にセットするcaseで上書きされるため安全）

        const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);

        

       // 新しい陣形（演出意図）に応じて、最適な「物理挙動（DroneState）」をセットする
        switch (drone.currentFormation) {

            case DroneFormationType::AllCharge:
                // 一斉突撃。上に浮かずに少し引く「水平突撃ステート」を使用する
                drone.currentState = DroneState::HorizontalCharge;

                if (playerTrans) {
                    Vector3 dir = playerTrans->position - drone.targetPosition;
                    if (dir.LengthSquared() > 0.001f) {
                        dir.Normalize();
                        drone.fireDirection = dir;
                        // ★ 視線指示：突撃方向を向く
                        drone.targetLookDirection = dir;
                    }
                    else {
                        drone.fireDirection = Vector3::UnitZ; // フェイルセーフ
                    }
                }
            break;

            case DroneFormationType::AegisShield:
                // 絶対防衛（盾）：フワフワしていると壁の隙間を抜けられてしまうため、
                // 素早くビシッと定位置に整列する「機械的移動」をセットする
                drone.currentState = DroneState::MoveToTarget;
                break;

            case DroneFormationType::BarrierBurst:
                drone.currentState = DroneState::LockOn;
                drone.stateTimer = 1.5f; // ★ 1.5秒間、力を溜める（ブルブル震える）
                break;

            case DroneFormationType::CycloneBurst:
                drone.currentState = DroneState::LockOn;
                drone.stateTimer = 2.5f; // 2.5秒間、回転しながらボスの中心へ引き絞るタメ
                break;

            case DroneFormationType::CrossHelix:
                // 弾幕のようにフワフワと有機的に交差させる
                drone.currentState = DroneState::Idle;
                break;

            case DroneFormationType::ChargeTunnel:
                drone.currentState = DroneState::MoveToTarget;
                drone.stateTimer = 0.0f; // アニメーション用のタイムラインを0から開始
                break;

            case DroneFormationType::ClapSlam:
                drone.currentState = DroneState::MoveToTarget;
                drone.stateTimer = 1.5f; // 予備動作 1.1s + 挟み込み 0.4s
                break;

            case DroneFormationType::DeathRing:

                // 追従・収縮モード。時間はBTに管理させるため、タイマーを「経過時間」として0から測る
                drone.currentState = DroneState::MoveToTarget;
                drone.stateTimer = 0.0f;

                break;

            case DroneFormationType::DirectionalCharge:
                // ★修正: Movementの「HorizontalChargeのタメ機能」に丸投げする
                drone.currentState = DroneState::HorizontalCharge;
                drone.stateTimer = 0.8f; // 0.8秒間 Movement側で震えてから自動発射される

                if (playerTrans) {
                    Vector3 dir = playerTrans->position - bossTrans.position;
                    dir.y = 0.0f; // 水平突撃のため、Y軸方向を潰して地面と平行に飛ばす
                    if (dir.LengthSquared() > 0.001f) {
                        dir.Normalize();
                        drone.fireDirection = dir;
                        drone.targetLookDirection = dir;
                    }
                    else {
                        drone.fireDirection = Vector3::UnitZ;
                    }
                }
                break;
            case DroneFormationType::GuillotineSlam:
                drone.currentState = DroneState::MoveToTarget;
                drone.stateTimer = 1.5f; // 予備動作 1.0s + 振り下ろし 0.5s
                break;
            case DroneFormationType::Hidden:
                break;
            case DroneFormationType::HighOrbit: 
                // 高軌道：ボスの頭上高くで旋回させる場合も、フワフワと有機的に追従させる]
                // 遠くから素早く頭上に戻したい場合は、IdleではなくMoveToTargetがおすすめです。
                drone.currentState = DroneState::MoveToTarget;
                break;

            case DroneFormationType::InfinityEight:
                // 【輪廻の軌道】
                // 上空で8の字の軌道を正確に描かせるため、これも MoveToTarget を指定。
                // (もしフワフワさせたい場合は Idle でも面白い挙動になります)
                drone.currentState = DroneState::MoveToTarget;
                break;

            case DroneFormationType::LowOrbit:
                // フワフワと有機的に追従させる
                drone.currentState = DroneState::Idle;
                break;

            case DroneFormationType::OrbitCircle:
                // 円陣：フワフワしていると全体の形が崩れてしまうため、素早くビシッと定位置に整列する「機械的移動」をセットする
                drone.currentState = DroneState::MoveToTarget;
                break;

            case DroneFormationType::SpreadLockOn:
                // 所定の位置へ素早く移動してピタッと止まるための機械的移動
                drone.currentState = DroneState::MoveToTarget;
                break;

       
            case DroneFormationType::HoldPosition:
                // ★修正: MoveToTarget ではなく、専用の「完全停止（Hold）」ステートを指定する
                drone.currentState = DroneState::Hold;
                break;
           
                //  第２形態
            case DroneFormationType::HighLowOrbit:
                // 素早く2段に分かれて整列させる
                drone.currentState = DroneState::MoveToTarget;
                break;

     
            case DroneFormationType::KamikazeSwarm:
                // ★ 既存の順番突撃の「凶悪版」
                drone.currentState = DroneState::LockOn;
                // タメ時間を 0.3s -> 0.05s に極限まで短縮！雨あられと降り注ぐ
                drone.stateTimer = drone.localIndex * 0.05f;
                break;
            case DroneFormationType::HighLowCycloneBurst:
                drone.currentState = DroneState::LockOn;

                // ★究極のハック: タイマーをグループごとにずらす！
                // 偶数(下段)は1.0秒、奇数(上段)は1.5秒で発射させる
                if (drone.localIndex % 2 == 0) {
                    drone.stateTimer = 1.5f;
                }
                else {
                    drone.stateTimer = 2.5f;
                }
                break;
            case DroneFormationType::SweepingBeam:
                // 【絶望の秒針】
                // レーザーの「刃」としてビシッと直線を維持してほしいため、
                // フワフワする Idle ではなく、正確に追従する MoveToTarget を指定します。
                drone.currentState = DroneState::MoveToTarget;
                break;

            case DroneFormationType::HighOrbitSnipe:
                drone.currentState = DroneState::LockOn;
                // 0.8秒間隔で、ゆっくりと威圧感を持たせて発射する
                drone.stateTimer = drone.localIndex * 0.8f;
                break;


            case DroneFormationType::SwarmRiver:
                drone.currentState = DroneState::MoveToTarget;

                // ====================================================================
                // 代表して先頭(0番)のドローンが、仮想リーダーと履歴バッファを初期化する
                // ====================================================================
                if (drone.localIndex == 0) {
                    std::lock_guard<std::mutex> lock(s_pathMutex);

                    // 初期位置はボスの現在地から 25m 上空
                    s_leaderPos = bossTrans.position + DirectX::SimpleMath::Vector3(0, 25.0f, 0);

                    if (const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId)) {
                        s_leaderDir = playerTrans->position - s_leaderPos;
                        s_leaderDir.Normalize();
                    }
                    else {
                        s_leaderDir = DirectX::SimpleMath::Vector3::UnitZ;
                    }

                    s_pathHead = 0;
                    s_pathCount = 0;
                    s_path[0] = { s_leaderPos, s_leaderDir };
                }

                drone.stateTimer = 0.0f;
                break;
            case DroneFormationType::TsunamiWave:
                // 波のような流体的なうねりを持たせるため、バネ制御の Idle を採用
                drone.currentState = DroneState::Idle;
                break;
            case DroneFormationType::SideSlapWhip:
                drone.currentState = DroneState::MoveToTarget;
                drone.stateTimer = 1.5f; // 予備動作 1.1s + 薙ぎ払い 0.4s
                break;
            case DroneFormationType::InfinitySnipe:
                // 軌道を描きながらタメるため、LockOn状態にする
                drone.currentState = DroneState::LockOn;

                // ====================================================================
                 // ★ 究極のタイムライン制御：号砲（予兆）と怒涛の連射
                 // 物理的なタメ時間は全員同じにし、「発射までの待機時間」の数式で間を作る
                 // ====================================================================
                if (drone.localIndex == 0) {
                    // [第1波：予兆の号砲] 
                    // 最初の1機（0番）は、陣形を展開してから「1.5秒後」に突撃を開始する。
                    drone.stateTimer = 1.5f;
                }
                else {
                    // [第2波：絶望の連続射撃]
                    // 1発目が撃たれた後、少しだけ間（0.5秒）を空けてから、
                    // 残りの全ドローンが「0.08秒間隔（超連射）」で降り注ぐ。

                    float firstShotTime = 1.5f;       // 1発目の発射時間
                    float pauseAfterFirst = 0.5f;     // 1発目と2発目の「絶望の間」
                    float rapidFireInterval = 0.2f;  // 2発目以降の連射スピード (マシンガン並み)

                    // (自分の番号 - 1) を掛けることで、1番目から順に0.08秒ずつズラす
                    drone.stateTimer = firstShotTime + pauseAfterFirst + ((drone.localIndex - 1) * rapidFireInterval);
                }
                break;
            case DroneFormationType::MegaClapSlam_LowerFirst:
                drone.currentState = DroneState::MoveToTarget;
                drone.stateTimer = 3.4f;
                break;
            case DroneFormationType::MegaClapSlam_UpperFirst:
                drone.currentState = DroneState::MoveToTarget;
                drone.stateTimer = 3.4f; 
                break;
                // メガ・シールドチャージの初期化（全体演出 3.5秒）
            case DroneFormationType::MegaShieldCharge_UpperFirst:
            case DroneFormationType::MegaShieldCharge_LowerFirst:
                drone.currentState = DroneState::MoveToTarget;
                drone.stateTimer = 3.5f;
                break;
            default:
                drone.currentState = DroneState::Idle;
                break;
        }
    }
}


// 1. 円陣（OrbitCircle）
inline void DroneFormationSystem::CalculateOrbitCircle(DroneComponent& drone, const TransformComponent& bossTrans, float currentTime) {
    // 【調整パラメーター】
    // 1.0f = 旋回スピード（大きいほど高速で回る。マイナスにすると逆回転）
    float speed = 1.0f;

    // 角度の計算： (時間 × スピード) + (360度 ÷ ドローン総数 × 自分の番号)
    float angle = currentTime * speed + (DirectX::XM_2PI / drone.totalDrones) * drone.localIndex;

    // 【調整パラメーター】
    // drone.orbitRadius = 旋回の広さ（現在はコンポーネントから取得）
    // 0.0f = ボスからの高さ（+2.0fなどにするとボスの少し上を回る）
    float heightOffset = 1.0f;

    Vector3 offset(cosf(angle) * drone.orbitRadius, heightOffset, sinf(angle) * drone.orbitRadius);
    drone.targetPosition = bossTrans.position + offset;

    // ★ 視線指示：円の中心から外側へ向かうベクトル（offset）を向かせる
    Vector3 lookDir = offset;
    lookDir.y = 0.0f; // 水平を向かせる
    if (lookDir.LengthSquared() > 0.001f) {
        lookDir.Normalize();
        drone.targetLookDirection = lookDir;
    }
}


// ============================================================================
// 上空狙撃 (HighOrbitSnipe) : 上空待機軌道から順番に狙撃し、一定距離で急停止する
// ============================================================================
inline void DroneFormationSystem::CalculateHighOrbitSnipe(DroneComponent& drone, const TransformComponent& bossTrans, const BossCommandComponent& bossCmd, float dt, float currentTime) {

    if (drone.currentState == DroneState::LockOn) {
        drone.stateTimer -= dt;

        if (drone.stateTimer <= 0.0f) {
            // ========================================================
            // [発射フェーズ]
            // ========================================================
            drone.currentState = DroneState::FireCharge;

            // 筋肉(MovementSystem)のタメ・後退演出用に0.5秒セット
            float anticipationTime = 0.5f;
            drone.stateTimer = anticipationTime;
            drone.hoverTimeOffset = 0.0f;

            const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);
            if (playerTrans) {
                // ====================================================
                // ★ 究極のハック：視差（パララックス）補正エイム
                // ====================================================
                // 1. プレイヤーの足元ではなく「胸のあたり」を的（ターゲット）にする
                Vector3 playerChestPos = playerTrans->position + Vector3(0.0f, 1.0f, 0.0f);

                // 2. まず、現在位置からの大まかな方向を出す
                Vector3 approxDir = playerChestPos - drone.targetPosition;
                if (approxDir.LengthSquared() > 0.001f) approxDir.Normalize();
                else approxDir = Vector3::UnitZ;

                // 3. 筋肉(MovementSystem)がタメ動作で移動する量を逆算する
                // （後ろに2.0のスピードで下がり、上に4.0のスピードで浮き上がる設定）
                float drawbackSpeed = 2.0f;
                float upwardLiftSpeed = 4.0f;

                // 4. 0.5秒後の「実際の発射座標（未来位置）」を予測計算
                Vector3 predictedStartPos = drone.targetPosition
                    + (Vector3::UnitY * (upwardLiftSpeed * anticipationTime))
                    + (-approxDir * (drawbackSpeed * anticipationTime));

                // 5. 予測座標からプレイヤーの胸へ向かう「完璧なベクトル」を叩き込む！
                Vector3 finalDir = playerChestPos - predictedStartPos;
                if (finalDir.LengthSquared() > 0.001f) {
                    finalDir.Normalize();
                    drone.fireDirection = finalDir;
                    drone.targetLookDirection = finalDir; // 首も正確な方向へ向ける
                }
            }
        }
        else {
            // ========================================================
            // [待機フェーズ] HighOrbitと全く同じ軌道計算をしてシームレスに繋ぐ
            // ========================================================
            float angle = currentTime * 1.0f + (DirectX::XM_2PI / drone.totalDrones) * drone.localIndex;
            Vector3 offset(cosf(angle) * drone.orbitRadius, 8.0f, sinf(angle) * drone.orbitRadius);
            drone.targetPosition = bossTrans.position + offset;

            // ★ 待機中は常にプレイヤーを睨みつける（殺意の演出）
            const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);
            if (playerTrans) {
                Vector3 toPlayer = playerTrans->position - drone.targetPosition;
                if (toPlayer.LengthSquared() > 0.001f) {
                    toPlayer.Normalize();
                    drone.targetLookDirection = toPlayer;
                }
            }
        }
    }
    else if (drone.currentState == DroneState::FireCharge) {
        // ========================================================
        // [飛行制限フェーズ] 飛びすぎ防止
        // ========================================================
        // hoverTimeOffset は MovementSystem で毎フレーム加算されている。
        // 0.5秒のタメ後退 ＋ 1.0秒の高速突撃 ＝ 計1.5秒経過したら空中で停止させる
        if (drone.hoverTimeOffset > 2.5f) {
            // ★ 空中で急ブレーキをかけて、その場に留まる（空間に突き刺さるような演出）
            drone.currentState = DroneState::Hold;
        }
    }
}

// 1. 処刑の輪（プレイヤーの周囲を囲む）
// ---------------------------------------------------------
// DroneFormationSystem.cpp
// ---------------------------------------------------------
inline void DroneFormationSystem::CalculateDeathRing(DroneComponent& drone, const BossCommandComponent& bossCmd, float dt, float currentTime) {
    const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);
    if (!playerTrans) return;

    // ========================================================
    // 【 調整パラメーター 】
    // ========================================================
    const float INITIAL_RADIUS = 20.0f;    // 展開時の外周半径（広めに取る）
    const float MIN_RADIUS = 6.5f;     // 収縮の限界値（檻の狭さ）
    const float HIGH_ALTITUDE = 18.0f;    // 最初の上空待機時の高さ
    const float TARGET_HEIGHT = 1.2f;     // 収縮完了時の高さ

    const float WAIT_DURATION = 0.5f;   // 上空で旋回するだけの時間
    const float DESCENT_DURATION = 1.2f;   // 垂直降下にかかる時間
    const float SHRINK_SPEED = 8.0f;   // 地面で縮むスピード (m/s)
    const float ROTATION_SPEED = 1.5f;   // 回転の速さ
    // ========================================================

    drone.stateTimer += dt;
    float t = drone.stateTimer;

    // 1. 高さ(Height)の制御：垂直降下のフェーズ
    float currentHeight = HIGH_ALTITUDE;
    if (t > WAIT_DURATION) {
        // 垂直降下の進行度 (0.0 ～ 1.0)
        float descentProgress = (t - WAIT_DURATION) / DESCENT_DURATION;
        descentProgress = std::clamp(descentProgress, 0.0f, 1.0f);

        // 線形補間（Lerp）で上空から地上へ垂直に降りる
        currentHeight = std::lerp(HIGH_ALTITUDE, TARGET_HEIGHT, descentProgress);
    }

    // 2. 半径(Radius)の制御：降下しきってから縮まり始める
    // 降下が終わるまでは INITIAL_RADIUS を維持することで、プレイヤーにぶつかるのを防ぐ
    float shrinkStartTime = WAIT_DURATION + DESCENT_DURATION;
    float currentRadius = INITIAL_RADIUS;

    if (t > shrinkStartTime) {
        // 降下完了後、時間の経過とともに半径を削っていく
        currentRadius = INITIAL_RADIUS - (SHRINK_SPEED * (t - shrinkStartTime));
    }
    // セーフティーロック
    currentRadius = (std::max)(currentRadius, MIN_RADIUS);

    // 3. 座標と回転の計算
    // 常に回転し続けることで「生きている檻」を表現
    float finalAngle = (t * ROTATION_SPEED) + (DirectX::XM_2PI / drone.totalDrones) * drone.localIndex;

    Vector3 offset(
        cosf(finalAngle) * currentRadius,
        currentHeight,
        sinf(finalAngle) * currentRadius
    );

    // プレイヤーの位置にオフセットを乗せて目標座標を更新
    drone.targetPosition = playerTrans->position + offset;

    // 4. 向きの制御：降下中は進行方向、収縮中は徐々に中心を睨む
    if (t < shrinkStartTime) {
        // [フェーズ1 & 2] 外周を回りながら降りてくる間は移動方向を向く
        drone.targetLookDirection = Vector3(-sinf(finalAngle), 0.0f, cosf(finalAngle));
    }
    else {
        // [フェーズ3] 地面で収縮を開始したら、中心（プレイヤー）へ顔を向ける
        Vector3 lookDir = -offset;
        lookDir.y = 0.0f;
        if (lookDir.LengthSquared() > 0.001f) lookDir.Normalize();
        drone.targetLookDirection = lookDir;
    }
}

// ============================================================================
// 絶対防衛陣形（AegisShield）: ボスの前面に壁を作る
// ============================================================================
inline void DroneFormationSystem::CalculateAegisShield(DroneComponent& drone, const TransformComponent& bossTrans, const BossCommandComponent& bossCmd) {
    const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);
    if (!playerTrans) return;

    Vector3 toPlayer = playerTrans->position - bossTrans.position;
    toPlayer.y = 0.0f; // 水平面のみで考える
    if (toPlayer.LengthSquared() > 0.001f) {
        toPlayer.Normalize();
    }
    else {
        toPlayer = Vector3::UnitZ;
    }

    Vector3 right = toPlayer.Cross(Vector3::UnitY);

    // ====================================================================
    // 【調整パラメーター】 グリッド設定とスケーリング
    // ====================================================================
    int maxColumns = 6;    // 第1形態の横幅（6機）
    float spacingX = 1.8f; // 横の隙間 
    float spacingY = 1.5f; // 縦の隙間
    float forwardOffset = 3.0f; // ボスからの距離

    // ★ 絶望の盾：第2形態(128機)の場合は、アリーナを分断する巨大な壁に進化する
    if (drone.totalDrones >= 64) {
        maxColumns = 20;      // 横幅を20機に拡大し、回避不能な大津波にする (幅約38m)
        spacingX = 2.0f;      // 圧迫感を出すため少し間隔を広げる
        spacingY = 1.6f;
        forwardOffset = 4.5f; // 壁が巨大になるため、ボスから少し前へ離す
    }

    Vector3 shieldCenter = bossTrans.position + toPlayer * forwardOffset;

    // 割り算と余りでグリッド配置を計算
    int row = drone.localIndex / maxColumns; // 段数 (0, 1, 2...)
    int col = drone.localIndex % maxColumns; // 横位置 (0, 1, 2, ...)

    int dronesInThisRow = (std::min)(maxColumns, drone.totalDrones - row * maxColumns);

    float wallWidth = (dronesInThisRow - 1) * spacingX;
    float startOffset = -wallWidth * 0.5f;

    float currentOffsetX = startOffset + col * spacingX;
    float currentOffsetY = row * spacingY;

    // 最終座標の決定
    drone.targetPosition = shieldCenter + right * currentOffsetX + Vector3(0, 1.0f + currentOffsetY, 0);
    drone.targetLookDirection = toPlayer;
}


// 1. 上空待機 (HighOrbit)
inline void DroneFormationSystem::CalculateHighOrbit(DroneComponent& drone, const TransformComponent& bossTrans, float currentTime) {

    // 【調整パラメーター】 1.0f = 上空での旋回スピード
    float angle = currentTime * 1.0f + (DirectX::XM_2PI / drone.totalDrones) * drone.localIndex;
    // 【調整パラメーター】 8.0f = 上空待機時の高さ（Y座標のオフセット）
    Vector3 offset(cosf(angle) * drone.orbitRadius, 8.0f, sinf(angle) * drone.orbitRadius); // ★Y軸に +8.0f (頭上)
    drone.targetPosition = bossTrans.position + offset;

    // ★ 視線指示：円の中心から外側へ向かうベクトル（offset）を向かせる
    Vector3 lookDir = offset;
    lookDir.y = 0.0f; // 水平を向かせる
    if (lookDir.LengthSquared() > 0.001f) {
        lookDir.Normalize();
        drone.targetLookDirection = -lookDir;
    }
}

// 2. 降下展開 (LowOrbit)
inline void DroneFormationSystem::CalculateLowOrbit(DroneComponent& drone, const TransformComponent& bossTrans, float currentTime) {
    // 【調整パラメーター】 0.5f = 降下後の旋回スピード
    float angle = currentTime * 1.0f + (DirectX::XM_2PI / drone.totalDrones) * drone.localIndex;

    // ====================================================================
    // ★修正: ボスのスケールに合わせて、高さを 4.0f から引き上げる
    // 例: ボスの胸あたりにしたい場合は 10.0f や 12.0f などに調整してください
    // ====================================================================
    float heightOffset = 1.5f;

  
    Vector3 offset(cosf(angle) * drone.orbitRadius, heightOffset, sinf(angle) * drone.orbitRadius); // ★Y軸は heightOffset (ボスと同じ高さ)
    drone.targetPosition = bossTrans.position + offset;

    // ボスの中心から外側へ向かうベクトル（offset）を視線にする
    Vector3 lookDir = offset;
    lookDir.y = 0.0f;
    if (lookDir.LengthSquared() > 0.001f) lookDir.Normalize();
    drone.targetLookDirection = lookDir;
}

// 3. 拡散静止 (SpreadLockOn)
inline void DroneFormationSystem::CalculateSpreadLockOn(DroneComponent& drone, const TransformComponent& bossTrans, float currentTime) {
    // 回転を止めたいので、currentTimeではなく固定値で角度を算出する（ボス基準で均等に配置）
    float angle = (DirectX::XM_2PI / drone.totalDrones) * drone.localIndex;
    // 攻撃の予兆として、半径を少し広げる (orbitRadius * 1.5f)
    // 【調整パラメーター】 1.5f = 攻撃の予兆として広がる大きさの倍率、1.0f = 展開時の高さ
    Vector3 offset(cosf(angle) * (drone.orbitRadius * 1.5f), 1.0f, sinf(angle) * (drone.orbitRadius * 1.5f));
    drone.targetPosition = bossTrans.position + offset;


    // ボスの中心から外側へ向かうベクトル（offset）を視線にする
    Vector3 lookDir = offset;
    lookDir.y = 0.0f;
    if (lookDir.LengthSquared() > 0.001f) lookDir.Normalize();
    drone.targetLookDirection = lookDir;
}



// ============================================================================
// システム専用GUIの描画（エディタ実行時のみ呼ばれる）
// ============================================================================
void DroneFormationSystem::OnGui()
{

    // システムのコントロールパネルを展開
    if (ImGui::CollapsingHeader("Drone Controller (Swarm Management)", ImGuiTreeNodeFlags_DefaultOpen)) {

        ImGui::Indent();

        // 1. 生成パラメータの設定
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Spawn Settings");
        ImGui::SliderInt("Spawn Count", &m_guiDroneCount, 1, 24);
        ImGui::SliderFloat("Spawn Radius", &m_guiOrbitRadius, 2.0f, 20.0f);
        ImGui::SliderFloat("Spawn Speed", &m_guiMoveSpeed, 10.0f, 100.0f);

        ImGui::Spacing();

        // 2. 現在のドローン数の取得（ECSのViewを使って数える）
        int currentDroneCount = 0;
        auto droneView = _world->View<DroneComponent>();
        for (auto e : droneView) {
            currentDroneCount++;
        }
        ImGui::Text("Current Active Drones: %d", currentDroneCount);

        ImGui::Spacing();

        // 3. ボスの検索（誰の周りにドローンを出すか）
        CCL::ECS::EntityID activeBossId = CCL::ECS::InvalidEntityID;
        auto bossView = _world->View<BossCommandComponent, TransformComponent>();
        for (auto b : bossView) {
            activeBossId = b;
            break; // 最初のボスを見つけたら終了
        }

        if (activeBossId == CCL::ECS::InvalidEntityID) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Warning: No Boss found in scene!");
        }
        else {
            // =========================================================
             // 【アクション】ドローンの動的追加
             // =========================================================
            if (ImGui::Button("Spawn Drones", ImVec2(120, 30))) {

                auto* bossTrans = _world->GetComponent<TransformComponent>(activeBossId);
                Vector3 spawnBasePos = bossTrans ? Vector3(bossTrans->position) : Vector3::Zero;
                spawnBasePos.y += 5.0f;

                // 指定された数だけ生成
                for (int i = 0; i < m_guiDroneCount; ++i) {
                    EntityID droneId = Prefab::SpawnPrefab(*_world, "Data/Prefabs/Enemy/Drone.json");
     

                    auto* transComp = _world->GetComponent<TransformComponent>(droneId);
                    if (transComp)
                    {
                        transComp->position = spawnBasePos;
                        transComp->isDirty = true;
                    }

                    auto* droneComp = _world->GetComponent<DroneComponent>(droneId);
                    if (droneComp) {
                        droneComp->ownerBossId = activeBossId;
                    }

                    auto* timeComp = _world->GetComponent<TimeState>(droneId);
                    if (timeComp) {
                        timeComp->group = TimeGroup::Enemy;
                    }
                }
            }

            ImGui::SameLine();

            // =========================================================
            // 【アクション】ドローンの全消去
            // =========================================================
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Destroy All Drones", ImVec2(150, 30))) {

                // ECS上でコンポーネントを持っているEntityを列挙して破棄
                // ※ _world->DestroyEntity(e) が安全にキューイングされる設計である前提
                auto view = _world->View<DroneComponent>();
                for (auto e : view) {
                    _world->Destroy(e);
                }
            }
            ImGui::PopStyleColor();
        }

        ImGui::Unindent();
    }
}


// ============================================================================
// 全方位密着バリア (CloseGuard) : フィボナッチ半球（ドーム）アルゴリズム
// ============================================================================
inline void DroneFormationSystem::CalculateCloseGuard(DroneComponent& drone, const TransformComponent& bossTrans, float currentTime) {
    // 【調整パラメーター】
    float tightRadius = 3.0f;  // ボスをすっぽり覆うドームの半径（ボスのサイズに合わせて広げてください）
    float rotateSpeed = 1.5f;  // バリア全体の回転スピード

    // 1. 自分は何番目のドローンか、0.0(先頭) 〜 1.0(最後尾) の割合にする
    float indexRatio = 0.0f;
    if (drone.totalDrones > 1) {
        indexRatio = (float)drone.localIndex / (float)(drone.totalDrones - 1);
    }

    // ====================================================================
    // ★修正1: 完全な球 (1.0 to -1.0) ではなく、上半分の「半球 (1.0 to 0.0)」にする
    // indexRatio を 2倍 しないことで、高さが地面（0.0）でピッタリ止まります。
    // ====================================================================
    float y = 1.0f - indexRatio;

    // 3. その高さにおける、球の「水平方向の半径」を三平方の定理で計算
    float radiusAtY = std::sqrt(1.0f - y * y);

    // 4. 黄金角（約137.5度 = 2.39996ラジアン）を用いて螺旋配置
    float goldenAngle = 2.399963f;
    float theta = goldenAngle * drone.localIndex + (currentTime * rotateSpeed);

    // 5. XとZの座標を計算
    float x = std::cos(theta) * radiusAtY;
    float z = std::sin(theta) * radiusAtY;

    // ====================================================================
    // ★修正2: heightOffset を廃止。
    // ボスの原点（足元）を中心に、上半分のドームを展開します。
    // ====================================================================
    Vector3 offset(x * tightRadius, (y * tightRadius) + 0.5f, z * tightRadius);
    drone.targetPosition = bossTrans.position + offset;

    // ボスの中心から外側へ向かうベクトル（offset）を視線にする
    Vector3 lookDir = offset;
    lookDir.y = 0.0f;
    if (lookDir.LengthSquared() > 0.001f) lookDir.Normalize();
    drone.targetLookDirection = lookDir;
}

// ============================================================================
// バリア爆発 (BarrierBurst) : 収縮 → 全方位拡散射出
// ============================================================================
inline void DroneFormationSystem::CalculateBarrierBurst(DroneComponent& drone, const TransformComponent& bossTrans, float dt, float currentTime) {
    if (drone.currentState == DroneState::LockOn) {
        // [フェーズ1] 溜め中：ボスの中心に向かってギュッと収縮する
        drone.stateTimer -= dt;

        if (drone.stateTimer <= 0.0f) {
            // [フェーズ2] 溜め完了：全方位へ一斉射出！
            drone.currentState = DroneState::FireCharge;

            // 射出方向は、「ボスの中心から、現在のドローンの配置位置」の延長線上
            Vector3 dir = drone.targetPosition - bossTrans.position;
            if (dir.LengthSquared() > 0.001f) {
                dir.Normalize();
                // ★修正: 無限遠方へのハックを削除し、純粋な方向ベクトルのみを渡す
                drone.fireDirection = dir;
            }
        }
        else {
            // ====================================================================
            // --- 収縮フォーメーションの維持（滑らかな溜め演出） ---
            // ====================================================================

            // 1. 溜めの進行度を 0.0(開始) 〜 1.0(爆発直前) で算出する
            float progress = 1.0f - (drone.stateTimer / 1.5f);
            progress = std::clamp(progress, 0.0f, 1.0f);

            // 2. 半径を CloseGuardの「3.0f」から、溜め完了時の「1.5f」へ滑らかに縮小させる
            float tightRadius = 3.0f - (1.5f * progress);

            // 3. 回転スピードも CloseGuard と完全に一致させる（ピタッと止めるとワープするため）
            float rotateSpeed = 1.5f;

            // --- 以下の計算式は CloseGuard と全く同じものを使用し、断絶を防ぐ ---
            float indexRatio = 0.0f;
            if (drone.totalDrones > 1) {
                indexRatio = (float)drone.localIndex / (float)(drone.totalDrones - 1);
            }

            float y = 1.0f - indexRatio;
            float radiusAtY = std::sqrt(1.0f - y * y);

            float goldenAngle = 2.399963f;
            float theta = goldenAngle * drone.localIndex + (currentTime * rotateSpeed);

            float x = std::cos(theta) * radiusAtY;
            float z = std::sin(theta) * radiusAtY;

            // Yのオフセットも CloseGuard と同じ「+0.5f」にする
            Vector3 offset(x * tightRadius, (y * tightRadius) + 0.5f, z * tightRadius);
            drone.targetPosition = bossTrans.position + offset;

            // タメ中 (else の中、offsetを計算した直後)
            Vector3 lookDir = offset;
            lookDir.y = 0.0f;
            if (lookDir.LengthSquared() > 0.001f) lookDir.Normalize();
            drone.targetLookDirection = lookDir; // ★タメ中は外側を向いて回る
        }
    }
}

// ============================================================================
// 決闘の回廊 (ChargeTunnel) : 追従展開 → 突進直前での完全固定
// ============================================================================
inline void DroneFormationSystem::CalculateChargeTunnel(DroneComponent& drone, const TransformComponent& bossTrans, const BossCommandComponent& bossCmd, float dt) {

    drone.stateTimer += dt;

    const auto* bossAction = _world->GetComponent<BossActionComponent>(drone.ownerBossId);
    if (!bossAction) return;

    // ====================================================================
    // ★ 修正: タイムリマッピング（元の動きを1ミリも崩さずに自動伸縮させる）
    // ====================================================================
    float time = drone.stateTimer;

    if (bossAction->currentState == BossActionState::Charge) {
        // ボスが突進開始(1.5秒前)までにタメられる実際の時間
        // (第1形態なら 3.5 - 1.5 = 2.0秒、第2形態なら 5.5 - 1.5 = 4.0秒)
        float windupTime = bossAction->currentChargeTotal - 1.5f;

        // あなたの元のコードが「一番奥のドローンまで完璧に完成する」までの設計時間
        // (幅寄せ開始 2.0 + 奥へのウェーブ遅延 1.0 + 幅寄せにかかる時間 0.5 = 3.5秒)
        float originalAnimDuration = 3.5f;

        // 実際の経過時間を、元のアニメーションのスケールに変換する（早回し / スロー再生）
        time = drone.stateTimer * (originalAnimDuration / windupTime);
    }

    // ====================================================================
    // ★ 究極のハイブリッド・ロックオンシステム
    // ボスのステートに応じて、トンネルの「基準点(Anchor)」と「方向(Dir)」を切り替える
    // ====================================================================
    Vector3 anchorPos;
    Vector3 forwardDir;

    if (bossAction->currentState == BossActionState::Charge) {
        // [突進・固定フェーズ] 
        // ボスが突進用のアクションに入ったら、ボスが記録した固定座標と方向を使う
        anchorPos = bossAction->chargeAnchorPosition;
        forwardDir = bossAction->actionDirection;
    }
    else {
        // [準備・追従フェーズ] 
        // まだタメている間は、ボスの現在地に追従し、プレイヤーを睨み続ける
        anchorPos = bossTrans.position;

        const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);
        if (playerTrans) {
            forwardDir = playerTrans->position - bossTrans.position;
            forwardDir.y = 0.0f;
            if (forwardDir.LengthSquared() > 0.001f) forwardDir.Normalize();
            else forwardDir = Vector3::UnitZ;
        }
        else {
            forwardDir = Vector3::UnitZ;
        }
    }

    // 確実な水平化
    forwardDir.y = 0.0f;

    // --- 以下の計算はすべて確定した「anchorPos」と「forwardDir」を基準に行う ---

    float tunnelLength = 20.0f;
    float sideSign = (drone.localIndex % 2 == 0) ? -1.0f : 1.0f;
    int dronesPerSide = drone.totalDrones / 2;
    int sideIndex = drone.localIndex / 2;

    // ★ 絶望の壁：ドローン数に応じた自動スケーリング
    int numX = 1; // 厚み（列数）
    int numY = 1; // 高さ（段数）

    if (dronesPerSide >= 32) {
        numX = 2; // 厚み2列
        numY = 3; // 高さ3段
        tunnelLength = 40.0f; // ★ 第2形態は長さを40mに伸ばす
    }

    int dronesPerSlice = numX * numY;
    int numZ = (dronesPerSide + dronesPerSlice - 1) / dronesPerSlice;

    int idxX = sideIndex % numX;
    int idxY = (sideIndex / numX) % numY;
    int idxZ = sideIndex / dronesPerSlice;

    float t_pos = (numZ > 1) ? (float)idxZ / (float)(numZ - 1) : 0.0f;

    // [高さの制御]
    float startHeight = 8.0f;
    float targetHeight = 1.5f;
    float currentY = startHeight;

    if (time > 1.0f) {
        float progressY = std::clamp((time - 1.0f) / 1.0f, 0.0f, 1.0f);
        progressY = progressY * progressY * (3.0f - 2.0f * progressY);
        currentY = std::lerp(startHeight, targetHeight, progressY);
    }

    currentY += (idxY * 1.5f);

    // [幅の制御]
    float startWidth = 10.0f;
    float targetWidth = 4.5f;
    float currentWidth = startWidth;

    float shrinkStartTime = 2.0f + (t_pos * 1.0f);

    if (time > shrinkStartTime) {
        float progressW = std::clamp((time - shrinkStartTime) / 0.5f, 0.0f, 1.0f);
        progressW = 1.0f - std::pow(1.0f - progressW, 3.0f);
        currentWidth = std::lerp(startWidth, targetWidth, progressW);
    }

    currentWidth += (idxX * 1.5f);

    // [最終座標の計算]
    Vector3 rightDir = Vector3(forwardDir.z, 0, -forwardDir.x);

    float zOffset = (idxX % 2 == 1) ? (tunnelLength / numZ) * 0.5f : 0.0f;
    float finalZ = (tunnelLength * t_pos) + zOffset;

    // ★ 最終合成：追従中は bossTrans、突進中は chargeAnchorPos (anchorPos) を使う！
    Vector3 offset = (forwardDir * finalZ) + (rightDir * (currentWidth * sideSign));

    drone.targetPosition = anchorPos + offset;
    drone.targetPosition.y = anchorPos.y + currentY;

    // 視線の制御
    drone.targetLookDirection = forwardDir;
}

// ============================================================================
// サイクロンバースト (CycloneBurst) : 回転しながら収縮 → 水平・全方位射出
// ============================================================================
inline void DroneFormationSystem::CalculateCycloneBurst(DroneComponent& drone, const TransformComponent& bossTrans, float dt, float currentTime) {
    if (drone.currentState == DroneState::LockOn) {
        drone.stateTimer -= dt;

        if (drone.stateTimer <= 0.0f) {
            drone.currentState = DroneState::FireCharge;

            Vector3 dir = drone.targetPosition - bossTrans.position;
            dir.y = 0.0f; // 地面と平行に飛ばす
            if (dir.LengthSquared() > 0.001f) {
                dir.Normalize();
                drone.fireDirection = dir;
            }
            else {
                drone.fireDirection = Vector3::UnitZ;
            }
        }
        else {
            // ====================================================================
            // 【調整パラメーター】 サイクロンバーストの演出設定
            // ====================================================================
            // ★重要: HandleStateTransitionでセットした時間(1.5f)と必ず一致させる！
            float totalChargeTime = 2.5f;

            float minRadius = 1.5f;           // どこまで小さく収縮するか
            float extraSpinTurns = 1.5f;      // タメ中に何周「追加で」回るか (1.5 = 1周半)

            // 加速カーブ (1.0=一定速度で吸い込まれる, 2.0=二次曲線で徐々に加速, 3.0=最後にギュイーンと急加速)
            float spinPower = 2.0f;

            // 1. 溜めの進行度 (0.0 〜 1.0) を正確に算出
            float progress = 1.0f - (drone.stateTimer / totalChargeTime);
            progress = std::clamp(progress, 0.0f, 1.0f);

            // 2. 半径の収縮
            float currentRadius = drone.orbitRadius - ((drone.orbitRadius - minRadius) * progress);

            // 3. ワープを防ぐ角度計算（スピンの追加）
            float baseAngle = currentTime * 1.0f + (DirectX::XM_2PI / drone.totalDrones) * drone.localIndex;
            // 指定したカーブ(spinPower)に従って、指定した周回数(extraSpinTurns)だけ余分に回す
            float spinAngle = std::pow(progress, spinPower) * (DirectX::XM_2PI * extraSpinTurns);
            float angle = baseAngle + spinAngle;

            // 4. 高さの持ち上げ
            float heightOffset = 1.0f + (0.5f * progress);

            Vector3 offset(cosf(angle) * currentRadius, heightOffset, sinf(angle) * currentRadius);
            drone.targetPosition = bossTrans.position + offset;

            // タメ中 (else の中、offsetを計算した直後)
            Vector3 lookDir = offset;
            lookDir.y = 0.0f;
            if (lookDir.LengthSquared() > 0.001f) lookDir.Normalize();
            drone.targetLookDirection = lookDir; // ★タメ中は外側を向いて回る
        }
    }
}


// ★ 1. 双層の輪廻 (HighLowOrbit)
inline void DroneFormationSystem::CalculateHighLowOrbit(DroneComponent& drone, const TransformComponent& bossTrans, float currentTime) {
    float speed = 1.2f; // 通常の円陣より少し速い
    float angle = currentTime * speed + (DirectX::XM_2PI / drone.totalDrones) * drone.localIndex;

    // 偶数(0, 2, 4...)はジャンプで避けさせる下段(0.5m)
    // 奇数(1, 3, 5...)はしゃがみや隙間で避けさせる上段(2.5m)
    float height = (drone.localIndex % 2 == 0) ? 0.5f : 2.5f;

    Vector3 offset(cosf(angle) * drone.orbitRadius, height, sinf(angle) * drone.orbitRadius);
    drone.targetPosition = bossTrans.position + offset;

    // ★ 視線指示：円の中心から外側へ向かうベクトル（offset）を向かせる
    Vector3 lookDir = offset;
    lookDir.y = 0.0f; // 水平を向かせる
    if (lookDir.LengthSquared() > 0.001f) {
        lookDir.Normalize();
        drone.targetLookDirection = lookDir;
    }
}

// ★ 2. 交差螺旋・双頭の蛇 (CrossHelix)
inline void DroneFormationSystem::CalculateCrossHelix(DroneComponent& drone, const BossCommandComponent& bossCmd, float currentTime) {
    const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);
    if (!playerTrans) return;

    // ボスではなく「プレイヤーの周囲」を回る殺意の高い陣形
    float speed = 1.5f;
    float radius = 8.0f; // プレイヤーを囲む檻の広さ

    // 偶数・奇数でグループを分け、回転方向（speedの符号）を逆にする！
    float direction = (drone.localIndex % 2 == 0) ? 1.0f : -1.0f;

    // 各グループ内でのインデックス(0, 1, 2...)を計算して均等に配置
    int groupIndex = drone.localIndex / 2;
    int groupTotal = drone.totalDrones / 2;

    float angle = currentTime * (speed * direction) + (DirectX::XM_2PI / groupTotal) * groupIndex;

    // 視覚的な美しさのため、回転しながらフワフワと高さを波打たせる（サイン波）
    float height = 1.5f + std::sin(currentTime * 3.0f + angle) * 1.0f;

    Vector3 offset(cosf(angle) * radius, height, sinf(angle) * radius);
    drone.targetPosition = playerTrans->position + offset;
}

// ============================================================================
// DroneFormationSystem.cpp : ファイル下部に追加
// ============================================================================
inline void DroneFormationSystem::CalculateHighLowCycloneBurst(DroneComponent& drone, const TransformComponent& bossTrans, float dt, float currentTime) {
    if (drone.currentState == DroneState::LockOn) {
        drone.stateTimer -= dt;

        // 規定時間に達したら発射！
        if (drone.stateTimer <= 0.0f) {
            drone.currentState = DroneState::FireCharge;

            Vector3 dir = drone.targetPosition - bossTrans.position;
            dir.y = 0.0f; // 地面と平行に飛ばす
            if (dir.LengthSquared() > 0.001f) {
                dir.Normalize();
                drone.fireDirection = dir;
            }
            else {
                drone.fireDirection = Vector3::UnitZ;
            }
        }
        else {
            // ====================================================================
            // 収縮・回転のタメ演出
            // ====================================================================
            bool isBottomLayer = (drone.localIndex % 2 == 0);

            // ★重要: HandleStateTransitionでセットした時間と必ず一致させる！
            float totalChargeTime = isBottomLayer ? 1.0f : 1.5f;

            float minRadius = 1.5f;
            float extraSpinTurns = 1.5f;
            float spinPower = 2.0f;

            // 各自のタイマーに合わせて進行度(0.0 〜 1.0)を独立して計算する
            float progress = 1.0f - (drone.stateTimer / totalChargeTime);
            progress = std::clamp(progress, 0.0f, 1.0f);

            float currentRadius = drone.orbitRadius - ((drone.orbitRadius - minRadius) * progress);

            // 完璧な「独立した2つの円」を作るためのグループ計算
            int dronesPerLayer = drone.totalDrones / 2;
            int layerIndex = drone.localIndex / 2;

            // ====================================================================
             // ★ 究極のバグ修正:
             // HighLowOrbit の回転スピード(1.2f)と完全に一致させる！
             // 以前はここが 1.0f になっていたため、時間が経つほどワープ距離が伸びていた。
             // ====================================================================
            float baseAngle = currentTime * 1.2f + (DirectX::XM_2PI / dronesPerLayer) * layerIndex;

            // 上段だけ角度をズラして、上下でドローンが重ならないようにする
            if (!isBottomLayer) {
                baseAngle += (DirectX::XM_PI / dronesPerLayer);
            }

            float spinAngle = std::pow(progress, spinPower) * (DirectX::XM_2PI * extraSpinTurns);
            float angle = baseAngle + spinAngle;

            // 高さを明確に2段に分け、収縮に合わせて少し浮き上がらせる
            float baseHeight = isBottomLayer ? 0.5f : 4.5f;
            float heightOffset = baseHeight + (0.5f * progress);

            Vector3 offset(cosf(angle) * currentRadius, heightOffset, sinf(angle) * currentRadius);
            drone.targetPosition = bossTrans.position + offset;
        }


    }
}


// ============================================================================
// ★ 128機専用：超量・群制御フォーメーション
// ============================================================================

// 1. 絶望の秒針 (Sweeping Beam)
inline void DroneFormationSystem::CalculateSweepingBeam(DroneComponent& drone, const TransformComponent& bossTrans, float currentTime) {
    // ------------------------------------------------------------------------
     // 1. スケーリング：第2形態では「二重の螺旋」を形成する
     // ------------------------------------------------------------------------
    bool isOuterSpiral = (drone.localIndex % 2 == 0); // 偶数は外側、奇数は内側
    int spiralIndex = drone.localIndex / 2;           // 各螺旋内でのインデックス
    int spiralCount = drone.totalDrones / 2;

    // ------------------------------------------------------------------------
    // 2. 螺旋のパラメータ（密度とリズム）
    // ------------------------------------------------------------------------
    float baseRadius = 5.0f;
    float maxRadius = 30.0f;

    // 時間経過で渦が呼吸するように半径を変動させる
    float breath = std::sin(currentTime * 0.8f) * 5.0f;

    // 螺旋の巻き数と回転速度
    float rotationSpeed = isOuterSpiral ? 0.6f : -0.9f; // 内と外で逆回転
    float angle = (currentTime * rotationSpeed) + (float(spiralIndex) / spiralCount * DirectX::XM_2PI * 3.0f);

    // 螺旋の広がり（外側は広く、内側はタイトに）
    float radius = baseRadius + (float(spiralIndex) / spiralCount * (maxRadius - baseRadius)) + breath;

    // ------------------------------------------------------------------------
    // 3. 高さとウェーブ（プレイヤーの回避を狩る）
    // ------------------------------------------------------------------------
    // プレイヤーのジャンプを狩るために、波を打たせる
    float height = 5.0f + std::sin(angle * 2.0f + currentTime * 2.0f) * 3.0f;

    // ------------------------------------------------------------------------
    // 4. 座標の計算（リサージュではなく、純粋な極座標の重ね合わせ）
    // ------------------------------------------------------------------------
    Vector3 pos(std::cos(angle) * radius, height, std::sin(angle) * radius);

    // 座標適用
    drone.targetPosition = bossTrans.position + pos;

    // ------------------------------------------------------------------------
    // 5. 威圧的な視線（常にプレイヤーを見つめる）
    // ------------------------------------------------------------------------
    // 前回実装した「常にプレイヤーを向く」視線ロジックをここでも再利用
    // (DroneFormationSystem内の共通ロジックを呼び出すか、直書きする)
    drone.targetLookDirection = Vector3::UnitZ; // 簡易：必要に応じてプレイヤー方向ベクトルを計算
}

// 2. 輪廻の軌道 (Infinity Eight)
inline void DroneFormationSystem::CalculateInfinityEight(DroneComponent& drone, const TransformComponent& bossTrans, float currentTime) {
    // リサージュ図形（Lissajous curve）の応用
    // X軸は sin(t)、Z軸は sin(2t) で計算すると、美しい8の字（無限大マーク）になる

    float t = currentTime * 1.0f + (DirectX::XM_2PI / drone.totalDrones) * drone.localIndex;

    float width = 25.0f;  // 8の字の横幅
    float depth = 10.0f;  // 8の字の縦幅
    float height = 15.0f; // ボスの頭上はるか高く

    float x = sinf(t) * width;
    float z = sinf(2.0f * t) * depth;

    Vector3 offset(x, height, z);
    drone.targetPosition = bossTrans.position + offset;

    // ★ 視線指示：プレイヤーを見下ろすように睨みつける
    Vector3 lookDir = -offset;
    if (lookDir.LengthSquared() > 0.001f) lookDir.Normalize();
    drone.targetLookDirection = lookDir;
}



// ============================================================================
// 暴虐の濁流 (Swarm River) : 仮想リーダーが軌跡を作り、後ろが過去の軌跡をなぞる
// ============================================================================
inline void DroneFormationSystem::CalculateSwarmRiver(DroneComponent& drone, const TransformComponent& bossTrans, const BossCommandComponent& bossCmd, float dt, float currentTime) {

    drone.stateTimer += dt;

    const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);
    Vector3 playerPos = playerTrans ? playerTrans->position + Vector3(0, 1.0f, 0) : bossTrans.position;

    // ====================================================================
    // ★ フェーズ1: 仮想リーダーのシミュレーション (0番機体が代表して道を作る)
    // ====================================================================
    if (drone.localIndex == 0) {
        std::lock_guard<std::mutex> lock(s_pathMutex);

        float moveSpeed = 35.0f; // 進行スピード
        float turnSpeed = 3.0f;  // 旋回スピード（プレイヤーを追う機敏さ）

        Vector3 toPlayer = playerPos - s_leaderPos;
        if (toPlayer.LengthSquared() > 0.001f) {
            toPlayer.Normalize();
            // 現在の進行方向からプレイヤーの方向へ、制限付きで曲がる
            s_leaderDir = Vector3::Lerp(s_leaderDir, toPlayer, turnSpeed * dt);
            s_leaderDir.Normalize();
        }

        // 仮想リーダーを前進させる
        s_leaderPos += s_leaderDir * (moveSpeed * dt);

        // ====================================================================
        // ★ 究極のバグ修正：リーダーの「地面這いずり（Ground Hugging）」処理
        // ====================================================================
        // リーダーが地面の下に潜り込んでしまうと、後からY軸だけ押し上げても
        // XとZの距離が稼げず、一箇所にドローンが密集して「縦1列に潰れる」現象が起きます。
        // ここでリーダーが地面に当たったら、進行ベクトルを強制的に「水平」に曲げます。

        float leaderFloorHeight = 5.0f; // 断面の厚みを考慮したリーダーの下限高度
        if (s_leaderPos.y < leaderFloorHeight) {
            s_leaderPos.y = leaderFloorHeight;

            // 下を向いているなら、Y成分を殺して地面を滑らせる（大蛇のように）
            if (s_leaderDir.y < 0.0f) {
                s_leaderDir.y = 0.0f;
                if (s_leaderDir.LengthSquared() > 0.001f) {
                    s_leaderDir.Normalize();
                }
                else {
                    s_leaderDir = Vector3::UnitZ; // 真下を向いていた場合のフェイルセーフ
                }
            }
        }

        // 新しい座標をリングバッファの先頭に記録する
        s_pathHead = (s_pathHead + 1) % MAX_PATH;
        s_path[s_pathHead] = { s_leaderPos, s_leaderDir };

        if (s_pathCount < MAX_PATH) s_pathCount++;
    }

    // ====================================================================
    // ★ フェーズ2: 過去の軌跡からの読み取り（全機体が実行）
    // ====================================================================
    int numX = 3;
    int numY = 2;
    int numSlice = numX * numY; // 1断面あたり6機

    int idxX = drone.localIndex % numX;
    int idxY = (drone.localIndex / numX) % numY;
    int idxZ = drone.localIndex / numSlice; // 深さ（何列後ろか）

    // 後ろの機体ほど「過去の軌跡」を読み取る
    // ★ 1列あたり 5フレーム分（約0.08秒）遅延させて過去を読む
    int delayFrames = idxZ * 8;

    PathNode myNode;
    {
        std::lock_guard<std::mutex> lock(s_pathMutex);

        if (s_pathCount == 0) {
            myNode = { bossTrans.position, Vector3::UnitZ };
        }
        else {
            // 現在のHeadから自分の遅延分だけ過去のインデックスを計算
            int readIndex = s_pathHead - delayFrames;

            // バッファがまだ溜まっていない（出撃直後）場合は、一番古いお尻のデータを読む
            if (readIndex < (s_pathHead - s_pathCount + 1)) {
                readIndex = s_pathHead - s_pathCount + 1;
            }

            // リングバッファのループ処理 (マイナスになったら後ろから回す)
            if (readIndex < 0) {
                readIndex += MAX_PATH;
            }

            myNode = s_path[readIndex];
        }
    }

    // ====================================================================
    // フェーズ3: 断面の構築 (読み取った過去の基準点に対して、自分の立ち位置を決める)
    // ====================================================================
    float spacingX = 2.4f;
    float spacingY = 2.4f;
    float offsetX = (idxX - (numX - 1) * 0.5f) * spacingX;
    float offsetY = (idxY - (numY - 1) * 0.5f) * spacingY;

    // 軌跡の進行方向から、右(Right)と上(Up)のベクトルを算出
    Vector3 rightDir = Vector3(myNode.dir.z, 0.0f, -myNode.dir.x);
    if (rightDir.LengthSquared() > 0.001f) rightDir.Normalize();
    else rightDir = Vector3::UnitX;

    Vector3 upDir = rightDir.Cross(myNode.dir);
    upDir.Normalize();

    // ====================================================================
    // ★ 形状維持のグラウンド・スライド (Push Up ハック)
    // ====================================================================
    // 塊の「一番下の段（idxY == 0）」の縦オフセットを算出する
    float minOffsetY = (0 - (numY - 1) * 0.5f) * spacingY;

    // この塊が本来到達するはずの「最下端のY座標」を計算
    float lowestY = myNode.pos.y + (upDir.y * minOffsetY);

    // アリーナの地面の高さ（ドローンが床に擦らないための浮遊マージン込み）
    float floorHeight = 1.0f;

    // もし最下端が地面にめり込むなら、塊全体をその差分だけ「上へ押し上げる」
    if (lowestY < floorHeight) {
        float pushUpAmount = floorHeight - lowestY;
        myNode.pos.y += pushUpAmount; // レール全体を強引に持ち上げる

        // ★ 演出: 地面を這っている時は、顔（視線）が地面に突き刺さらないように少し上げる
        if (myNode.dir.y < -0.1f) {
            myNode.dir.y *= 0.5f; // 下向きの角度を緩和する
            myNode.dir.Normalize();
        }
    }

    // ====================================================================
    // 最終座標の合成 (ベース + 断面の立ち位置)
    // ====================================================================
    drone.targetPosition = myNode.pos + (rightDir * offsetX) + (upDir * offsetY);

    // 首の向きも、補正された方向に向ける
    drone.targetLookDirection = myNode.dir;
}

// AI/BehaviorTree/Drone/DroneFormationSystem.cpp

// ============================================================================
// 側面薙ぎ払い (SideSlapWhip) : ボスの右側に壁を作り、左へ一気にビンタする
// ============================================================================
inline void DroneFormationSystem::CalculateSideSlapWhip(DroneComponent& drone, const TransformComponent& bossTrans, float dt) {
    if (drone.currentState != DroneState::MoveToTarget && drone.currentState != DroneState::Shaking) return;

    drone.stateTimer -= dt;

    // ------------------------------------------------------------------------
    // 1. グリッド座標の算出（ドローン数に応じて巨大化）
    // ------------------------------------------------------------------------
    int columns = 8; // 長さ(リーチ)
    int rows = 4;    // 高さ

    // ★ 絶望の壁：第2形態(128機)など、ドローンが十分にある場合は壁を巨大化
    if (drone.totalDrones >= 64) {
        columns = 16; // リーチ倍増 (約25mの長大な刃)
        rows = 8;     // 高さ倍増 (約11mの壁。ジャンプ回避を許さない)
    }

    int col = drone.localIndex % columns;
    int row = (drone.localIndex / columns) % rows;

    // ボスからの距離（col=0が手前3m、col=先端）
    float distance = 3.0f + (col * 1.5f);
    // 地面からの高さ
    float height = 1.0f + (row * 1.5f);

    // ------------------------------------------------------------------------
    // 2. アニメーション（薙ぎ払い角度）の計算
    // ------------------------------------------------------------------------
    float swingAngleRad = 0.0f;

    if (drone.stateTimer > 0.4f) {
        // [フェーズ1] 予備動作 (タメ)
        float prepareProgress = (1.5f - drone.stateTimer) / 1.1f;
        swingAngleRad = -prepareProgress * 0.3f; // 少し背中側へ引く
    }
    else if (drone.stateTimer > 0.0f) {
        // [フェーズ2] 薙ぎ払いスイング
        float swingProgress = 1.0f - (drone.stateTimer / 0.4f); // 0.0 -> 1.0
        float curve = swingProgress * swingProgress; // Ease-In

        // 背中側(-0.3rad) から 左側(π rad) まで一気に振り抜く
        swingAngleRad = -0.3f + (curve * (DirectX::XM_PI + 0.3f));
    }
    else {
        // [フェーズ3] 振り抜き完了後、左側で停止
        swingAngleRad = DirectX::XM_PI;
    }

    // ------------------------------------------------------------------------
    // 3. 空間への適用 (行列・クォータニオン計算)
    // ------------------------------------------------------------------------
    DirectX::SimpleMath::Vector3 bossRight = DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3::UnitX, bossTrans.rotation);
    DirectX::SimpleMath::Quaternion swingRot = DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(DirectX::SimpleMath::Vector3::UnitY, -swingAngleRad);
    DirectX::SimpleMath::Vector3 currentDir = DirectX::SimpleMath::Vector3::Transform(bossRight, swingRot);

    drone.targetPosition = bossTrans.position + (currentDir * distance) + DirectX::SimpleMath::Vector3(0.0f, height, 0.0f);
    drone.targetLookDirection = DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3::UnitZ, bossTrans.rotation);

    // 【側面ビンタ：スイング開始は 0.4s】 直前の 0.5秒間（0.9s ～ 0.4s）で震える
    if (drone.stateTimer > 0.4f && drone.stateTimer <= 0.9f) {
        drone.currentState = DroneState::Shaking;
    }
    else if (drone.stateTimer <= 0.4f && drone.stateTimer > 0.0f) {
        drone.currentState = DroneState::MoveToTarget;
    }

}


inline void DroneFormationSystem::CalculateInfinitySnipe(DroneComponent& drone, const TransformComponent& bossTrans, const BossCommandComponent& bossCmd, float dt, float currentTime) {

    if (drone.currentState == DroneState::LockOn) {
        drone.stateTimer -= dt;

        if (drone.stateTimer <= 0.0f) {
            // ========================================================
            // [発射フェーズ] : タイマーが切れたらタメて突撃！
            // ========================================================
            drone.currentState = DroneState::FireCharge;

            // 筋肉(MovementSystem)のタメ・後退演出用に0.5秒セット
            float anticipationTime = 0.5f;
            drone.stateTimer = anticipationTime;
            drone.hoverTimeOffset = 0.0f;

            const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);

            if (playerTrans) {
                // 基本はプレイヤーの胸を狙う
                Vector3 targetPos = playerTrans->position + Vector3(0.0f, 1.0f, 0.0f);

                // ★ バグ修正: 奇数機のみ、移動コンポーネントが「取得できた場合だけ」偏差撃ちを試みる
                if (drone.localIndex % 2 != 0) {
                    const auto* playerMove = _world->GetComponent<CharacterMovementInputComponent>(bossCmd.targetPlayerId);
                    if (playerMove) {
                        float bulletSpeed = drone.moveSpeed * 2.5f;
                        targetPos = CalculatePredictiveTarget(drone.targetPosition, *playerTrans, *playerMove, bulletSpeed);
                    }
                }

                // ====================================================
                // 完璧な逆算エイム（パララックス補正）
                // ====================================================
                Vector3 approxDir = targetPos - drone.targetPosition;
                if (approxDir.LengthSquared() > 0.001f) approxDir.Normalize();
                else approxDir = Vector3::UnitZ;

                float drawbackSpeed = 2.0f;
                float upwardLiftSpeed = 4.0f;

                // 0.5秒のタメ動作で移動する「未来の自分の発射座標」を予測
                Vector3 predictedStartPos = drone.targetPosition
                    + (Vector3::UnitY * (upwardLiftSpeed * anticipationTime))
                    + (-approxDir * (drawbackSpeed * anticipationTime));

                // 未来の発射座標から、ターゲットへ向かう完璧なベクトルを算出
                Vector3 finalDir = targetPos - predictedStartPos;
                if (finalDir.LengthSquared() > 0.001f) {
                    finalDir.Normalize();
                    drone.fireDirection = finalDir;
                    drone.targetLookDirection = finalDir; // 首も正確な方向へ向ける
                }
            }
        }
        else {
            // ========================================================
            // [待機フェーズ] : 完全に純粋な InfinityEight の軌道を描く
            // ========================================================
            float t = currentTime * 1.0f + (DirectX::XM_2PI / drone.totalDrones) * drone.localIndex;

            float width = 25.0f;
            float depth = 10.0f;
            float height = 15.0f;

            // 余計な波打ちを削除し、純粋なリサージュ図形に固定
            Vector3 offset(std::sin(t) * width, height, std::sin(2.0f * t) * depth);
            drone.targetPosition = bossTrans.position + offset;

            // 待機中は常にプレイヤーを睨みつける
            if (const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId)) {
                Vector3 toPlayer = playerTrans->position - drone.targetPosition;
                if (toPlayer.LengthSquared() > 0.001f) {
                    toPlayer.Normalize();
                    drone.targetLookDirection = toPlayer;
                }
            }
        }
    }
    else if (drone.currentState == DroneState::FireCharge) {
        // ========================================================
        // [飛行制限フェーズ] 飛びすぎ防止
        // ========================================================
        if (drone.hoverTimeOffset > 2.5f) {
            drone.currentState = DroneState::Hold; // 空中で急停止
        }
    }
}


// ============================================================================
// 瀑布の津波 (Tsunami Wave) : ボスから滝のように降り、プレイヤーへ押し寄せる波
// ============================================================================
inline void DroneFormationSystem::CalculateTsunamiWave(DroneComponent& drone, const TransformComponent& bossTrans, const BossCommandComponent& bossCmd, float currentTime) {

    // 1. 波が流れる絶対方向（ボスからプレイヤーへ）
    const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);
    Vector3 forwardDir = Vector3::UnitZ;
    if (playerTrans) {
        forwardDir = playerTrans->position - bossTrans.position;
        forwardDir.y = 0.0f;
        if (forwardDir.LengthSquared() > 0.001f) forwardDir.Normalize();
    }
    Vector3 rightDir = Vector3(forwardDir.z, 0, -forwardDir.x);

    // ====================================================================
    // 2. 波のグループ分割 (128機 -> 32機 × 4つの連続する波)
    // ====================================================================
    int numWaves = 4;
    int dronesPerWave = drone.totalDrones / numWaves;
    if (dronesPerWave == 0) dronesPerWave = 1;

    int waveIndex = drone.localIndex / dronesPerWave;   // 自分が第何波か (0〜3)
    int droneInWave = drone.localIndex % dronesPerWave; // 波の中での立ち位置 (0〜31)

    // ====================================================================
    // 3. ループ時間の計算（波が次々と押し寄せるドミノ機構）
    // ====================================================================
    float waveInterval = 2.5f; // 2.5秒ごとに新しい波が降ってくる
    float cycleTime = numWaves * waveInterval;

    // 各波の開始時間をズラし、cycleTimeでループさせる
    float offsetTime = waveIndex * waveInterval;
    float localTime = std::fmod(currentTime + cycleTime - offsetTime, cycleTime);

    // ====================================================================
    // 4. 前進距離とU字型カーブの計算
    // ====================================================================
    float speed = 25.0f; // 波が押し寄せるスピード
    float flowZ = localTime * speed;

    // 湧き出し前のタメ（ボスの内部で待機させる時間的バッファ）
    float waitDistance = 5.0f;
    float activeZ = flowZ - waitDistance;
    if (activeZ < 0.0f) activeZ = 0.0f;

    // 横幅（1列）の展開
    float spacingX = 1.6f;
    float widthOffset = (droneInWave - (dronesPerWave - 1) * 0.5f) * spacingX;

    // 【レベルデザイン】両端のドローンを少し「前」に出し、U字型（三日月）にして横逃げを狩る
    float curveZ = std::pow(widthOffset * 0.1f, 2.0f) * 0.6f;
    float finalZ = activeZ + curveZ;

    // ====================================================================
    // 5. 「真ん中から下に降りる」落下の計算
    // ====================================================================
    // Z座標が 0m 〜 12m 進む間に、ボスの高さから地面へ滝のように落ちる
    float descentDistance = 12.0f;
    float dropProgress = std::clamp(finalZ / descentDistance, 0.0f, 1.0f);

    // イーズイン（最初はゆっくり、後から重力加速度で急降下する曲線）
    float dropEase = dropProgress * dropProgress * dropProgress;

    float floorHeight = 1.0f; // 地面スレスレの高さ
    // ボスの現在の高さを起点として、地面へLerpする
    float currentY = std::lerp(bossTrans.position.y, floorHeight, dropEase);

    // 【演出】地面を這う際、さらに波のように上下にうねらせる（ジャンプ回避のタイミングを揺さぶる）
    if (dropProgress >= 1.0f) {
        // 進行距離に基づくサイン波で、最大2.0mの高さまでうねる
        float ripple = std::sin(finalZ * 0.4f - currentTime * 6.0f) * 1.0f + 1.0f;
        currentY += ripple;
    }

    // ====================================================================
    // 6. 最終合成
    // ====================================================================
    Vector3 offset = (forwardDir * finalZ) + (rightDir * widthOffset);
    drone.targetPosition = bossTrans.position + offset;
    drone.targetPosition.y = currentY; // 高さは絶対値として上書き

    // 視線は波の進行方向を睨みつける
    drone.targetLookDirection = forwardDir;
}

// ============================================================================
// 縦割り攻撃 (Guillotine Slam) : 頭上に巨大な刃を形成し、正面へ一気に振り下ろす
// ============================================================================
inline void DroneFormationSystem::CalculateGuillotineSlam(DroneComponent& drone, const TransformComponent& bossTrans, float dt) {

    if (drone.currentState != DroneState::MoveToTarget && drone.currentState != DroneState::Shaking) return;

    // 既に振り下ろしが完了し、地面に刺さっているなら計算を放棄する
    if (drone.stateTimer <= -1.0f) return;

    drone.stateTimer -= dt;

    // ------------------------------------------------------------------------
    // 1. グリッド座標の算出（ドローン数に応じて超巨大な大剣へ進化）
    // ------------------------------------------------------------------------
    int columns = 3; // 幅
    int rows = 10;   // 長さ

    // ★ 覇王の剣：第2形態(128機)など、ドローンが十分にある場合は即死級の巨大剣に進化
    if (drone.totalDrones >= 64) {
        columns = 8;  // 刃の幅が広がり、横ダッシュ回避の猶予を激減させる (幅約7m)
        rows = 16;    // 刃の長さが伸び、後方への逃げを許さない (リーチ約24m)
    }

    int col = drone.localIndex % columns;
    int row = (drone.localIndex / columns) % rows;

    // 刃のローカル座標系（中心を0とするためオフセットを計算）
    // 偶数列でも奇数列でも美しく中心が揃う究極の数式
    float widthOffset = (col - (columns - 1) * 0.5f) * 1.0f;
    // 下から上へ伸びる長さ
    float lengthOffset = row * 1.5f;

    // ------------------------------------------------------------------------
    // 2. アニメーション（回転角度と、根元のスライド進行度）の計算
    // ------------------------------------------------------------------------
    float swingAngleRad = 0.0f;
    float slideProgress = 0.0f;

    if (drone.stateTimer > 0.5f) {
        // [フェーズ1] 予備動作 (タメ)
        float prepareProgress = (1.5f - drone.stateTimer) / 1.0f;
        swingAngleRad = -prepareProgress * 0.2f;
        slideProgress = 0.0f;
    }
    else if (drone.stateTimer > 0.0f) {
        // [フェーズ2] 振り下ろし
        float swingProgress = 1.0f - (drone.stateTimer / 0.5f);
        float curve = swingProgress * swingProgress * swingProgress; // 3乗加速で重みを出す

        swingAngleRad = -0.2f + (curve * (DirectX::XM_PIDIV2 + 0.3f));
        slideProgress = curve;
    }
    else {
        // [フェーズ3] 振り下ろし完了後
        swingAngleRad = DirectX::XM_PIDIV2 + 0.3f;
        slideProgress = 1.0f;
        drone.stateTimer = -2.0f; // 空間ロックフラグ
    }

    // ------------------------------------------------------------------------
    // 3. 空間への適用 (行列・クォータニオン計算)
    // ------------------------------------------------------------------------
    DirectX::SimpleMath::Vector3 bossForward = DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3::UnitZ, bossTrans.rotation);
    DirectX::SimpleMath::Vector3 bossRight = DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3::UnitX, bossTrans.rotation);
    DirectX::SimpleMath::Vector3 bossUp = DirectX::SimpleMath::Vector3::UnitY;

    DirectX::SimpleMath::Quaternion swingRot = DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(bossRight, swingAngleRad);

    // 刃の根元（ヒンジ）の動的スライド計算
    // A. 予兆時の根元（ボスの頭上 4.5m）
    DirectX::SimpleMath::Vector3 topBasePos = bossTrans.position + bossUp * 4.5f;
    // B. 振り下ろし完了時の根元（ボスの目の前：前に2.0m、高さ0.5m付近）
    DirectX::SimpleMath::Vector3 frontBasePos = bossTrans.position + (bossForward * 2.0f) + (bossUp * 0.5f);

    DirectX::SimpleMath::Vector3 bladeBasePos = DirectX::SimpleMath::Vector3::Lerp(topBasePos, frontBasePos, slideProgress);

    DirectX::SimpleMath::Vector3 localBladePos = (bossRight * widthOffset) + (bossUp * lengthOffset);
    DirectX::SimpleMath::Vector3 rotatedBladePos = DirectX::SimpleMath::Vector3::Transform(localBladePos, swingRot);

    drone.targetPosition = bladeBasePos + rotatedBladePos;
    drone.targetLookDirection = DirectX::SimpleMath::Vector3::Transform(bossUp, swingRot);

    // 【縦割り：スイング開始は 0.5s】 直前の 0.5秒間（1.0s ～ 0.5s）で震える
    if (drone.stateTimer > 0.5f && drone.stateTimer <= 1.0f) {
        drone.currentState = DroneState::Shaking;
    }
    else if (drone.stateTimer <= 0.5f && drone.stateTimer > 0.0f) {
        drone.currentState = DroneState::MoveToTarget;
    }
}


// ============================================================================
// 双刃挟み込み (Clap Slam) : プレイヤーの方向を中心軸とし、左右から挟み込む
// ============================================================================
inline void DroneFormationSystem::CalculateClapSlam(DroneComponent& drone, const TransformComponent& bossTrans, const BossCommandComponent& bossCmd, float dt) {

    if (drone.currentState != DroneState::MoveToTarget && drone.currentState != DroneState::Shaking) return;

    // ★追加: 既に挟み込みが完了し、閉じきっているなら計算を放棄して空間固定する (GuillotineSlam方式)
    if (drone.stateTimer <= -1.0f) return;

    drone.stateTimer -= dt;

    // ------------------------------------------------------------------------
    // 1. グループ分けと座標算出
    // ------------------------------------------------------------------------
    bool isRightSide = (drone.localIndex % 2 == 0);
    int groupIndex = drone.localIndex / 2;

    float distance = 3.0f + (groupIndex * 1.2f);
    float height = 1.0f;

    // ------------------------------------------------------------------------
    // 2. 他のコードに則り、bossCmd からプレイヤーの座標を安全に取得する
    // ------------------------------------------------------------------------
    DirectX::SimpleMath::Vector3 centerDir = DirectX::SimpleMath::Vector3::UnitZ; // フェイルセーフ用

    const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);
    if (playerTrans) {
        // ボスからプレイヤーへの絶対的な方向ベクトルを算出
        centerDir = playerTrans->position - bossTrans.position;
        centerDir.y = 0.0f; // Y軸を潰して水平面上のベクトルにする

        if (centerDir.LengthSquared() > 0.001f) {
            centerDir.Normalize();
        }
        else {
            centerDir = DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3::UnitZ, bossTrans.rotation);
        }
    }
    else {
        // プレイヤーを見失った場合はボスの正面を使う
        centerDir = DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3::UnitZ, bossTrans.rotation);
    }

    // ------------------------------------------------------------------------
    // 3. アニメーション角度（中心線を 0 ラジアン とした対称計算）
    // ------------------------------------------------------------------------
    float startAngle = isRightSide ? 2.0f : -2.0f;
    float endAngle = isRightSide ? 0.05f : -0.05f;
    float currentAngleRad = 0.0f;

    if (drone.stateTimer > 0.4f) {
        // [フェーズ1] 予備動作 (タメ)
        float prepareProgress = (1.5f - drone.stateTimer) / 1.1f;
        float initAngle = isRightSide ? DirectX::XM_PIDIV2 : -DirectX::XM_PIDIV2;

        currentAngleRad = initAngle + (startAngle - initAngle) * prepareProgress;
    }
    else if (drone.stateTimer > 0.0f) {
        // [フェーズ2] 挟み込み (Clap)
        float swingProgress = 1.0f - (drone.stateTimer / 0.4f);
        float curve = swingProgress * swingProgress;

        currentAngleRad = startAngle + (endAngle - startAngle) * curve;
    }
    else {
        // [フェーズ3] 激突完了後 (クロスクラップ)
        currentAngleRad = isRightSide ? -0.08f : 0.08f;

        // ★追加: 空間ロックフラグを立てて、次フレーム以降の追従計算を完全遮断する
        drone.stateTimer = -2.0f;
    }

    // ------------------------------------------------------------------------
    // 4. 空間への適用 
    // ------------------------------------------------------------------------
    DirectX::SimpleMath::Quaternion swingRot = DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(DirectX::SimpleMath::Vector3::UnitY, currentAngleRad);
    DirectX::SimpleMath::Vector3 finalDroneDir = DirectX::SimpleMath::Vector3::Transform(centerDir, swingRot);

    drone.targetPosition = bossTrans.position + (finalDroneDir * distance) + DirectX::SimpleMath::Vector3(0.0f, height, 0.0f);

    // ------------------------------------------------------------------------
    // ★ 5. 視線の制御：ドローン各自が「常にプレイヤーの方を向く」
    // ------------------------------------------------------------------------
    if (playerTrans) {
        // 現在の自分の目標座標から、プレイヤーの座標へのベクトルを算出
        DirectX::SimpleMath::Vector3 toPlayer = playerTrans->position - drone.targetPosition;
        toPlayer.y = 0.0f; // 上下には首を振らない（水平に睨む）

        if (toPlayer.LengthSquared() > 0.001f) {
            toPlayer.Normalize();
            drone.targetLookDirection = toPlayer;
        }
        else {
            drone.targetLookDirection = centerDir;
        }
    }
    else {
        drone.targetLookDirection = centerDir;
    }

    // 【挟み込み：スイング開始は 0.4s】 直前の 0.5秒間（0.9s ～ 0.4s）で震える
    if (drone.stateTimer > 0.4f && drone.stateTimer <= 0.9f) {
        drone.currentState = DroneState::Shaking;
    }
    else if (drone.stateTimer <= 0.4f && drone.stateTimer > 0.0f) {
        drone.currentState = DroneState::MoveToTarget;
    }
}

// ============================================================================
// メガ・クラップスラム (Mega Clap Slam) : 128機による上下時間差の多段挟み込み
// ============================================================================
inline void DroneFormationSystem::CalculateMegaClapSlam(DroneComponent& drone, const TransformComponent& bossTrans, const BossCommandComponent& bossCmd, float dt, bool isUpperFirst) {

    if (drone.currentState != DroneState::MoveToTarget && drone.currentState != DroneState::Shaking) return;

    // 完全に突き刺さった後は計算をスキップして空間ロック
    if (drone.stateTimer <= -1.0f) return;

    drone.stateTimer -= dt;

    // ------------------------------------------------------------------------
    // 1. 128機の超巨大マトリクス配置 (左右64機ずつ、縦4段 × 横16列)
    // ------------------------------------------------------------------------
    bool isRightSide = (drone.localIndex % 2 == 0);
    int sideIndex = drone.localIndex / 2;

    int row = sideIndex % 4;
    int col = sideIndex / 4;

    // 例：ボスから 4.0m 離れた位置から、1.4m 間隔で 16列 外側へ並べる場合
    float distance = 4.0f + (col * 1.4f);
    float height = 1.0f + (row * 1.8f);

    bool isMySegmentUpper = (row >= 2);

    // ------------------------------------------------------------------------
    // 2. ターゲット（プレイヤー）への中心線（正面軸）を算出
    // ------------------------------------------------------------------------
    DirectX::SimpleMath::Vector3 centerDir = DirectX::SimpleMath::Vector3::UnitZ;
    const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);
    if (playerTrans) {
        centerDir = playerTrans->position - bossTrans.position;
        centerDir.y = 0.0f;
        if (centerDir.LengthSquared() > 0.001f) centerDir.Normalize();
    }

    // ------------------------------------------------------------------------
    // ★修正: 3. 多段・時間差アニメーションのタイムライン計算（3.4s 完全同期版）
    // ------------------------------------------------------------------------
    float startAngle = isRightSide ? 2.2f : -2.2f;
    float endAngle = isRightSide ? 0.05f : -0.05f;

    float currentAngleRad = startAngle;

    if (drone.stateTimer > 1.4f) {
        // 【フェーズ1】共通予備動作 (3.4f から 1.4f までの 2.0秒間)
        // 3.4f のとき 0.0、1.4f のとき 1.0 になる絶対数式
        float prepareProgress = (3.4f - drone.stateTimer) / 2.0f;

        float initAngle = isRightSide ? DirectX::XM_PIDIV2 : -DirectX::XM_PIDIV2;
        currentAngleRad = initAngle + (startAngle - initAngle) * prepareProgress;
    }
    else if (drone.stateTimer > 1.0f) {
        // 【フェーズ2】第1波・挟み込み (1.4f から 1.0f までの 0.4秒間)
        // 1.4f のとき 0.0、1.0f のとき 1.0 になる絶対数式
        float progress = (1.4f - drone.stateTimer) / 0.4f;
        float curve = progress * progress; // 2乗加速

        // 自分が「先攻グループ」なら正面へスイング、違えば待機（翼を開いたままタメ）
        if (isUpperFirst == isMySegmentUpper) {
            currentAngleRad = startAngle + (endAngle - startAngle) * curve;
        }
        else {
            currentAngleRad = startAngle;
        }
    }
    else if (drone.stateTimer > 0.0f) {
        // 【フェーズ3】第2波・挟み込み (1.0f から 0.0f までの【1.0秒間】)
        // ★レベルデザイン要求: 1.0f のとき 0.0、0.0f のとき 1.0 になる絶対数式
        float progress = (1.0f - drone.stateTimer) / 1.0f;

        // 2乗カーブにより、1.0秒かけてじわじわと、しかし後半にかけて重厚に閉じ叩きつける！
        float curve = progress * progress;

        // 先攻グループは正面(endAngle)で交差を維持、後攻グループが満を持して重厚にスイング
        if (isUpperFirst == isMySegmentUpper) {
            currentAngleRad = endAngle;
        }
        else {
            currentAngleRad = startAngle + (endAngle - startAngle) * curve;
        }
    }
    else {
        // 【フェーズ4】全弾激突完了
        currentAngleRad = endAngle;
        drone.stateTimer = -2.0f; // 空間ロックフラグを立てて以降の計算を完全遮断
    }

    // ------------------------------------------------------------------------
    // 4. 行列適用と個別ロックオン
    // ------------------------------------------------------------------------
    DirectX::SimpleMath::Quaternion swingRot = DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(DirectX::SimpleMath::Vector3::UnitY, currentAngleRad);
    DirectX::SimpleMath::Vector3 finalDroneDir = DirectX::SimpleMath::Vector3::Transform(centerDir, swingRot);

    drone.targetPosition = bossTrans.position + (finalDroneDir * distance) + DirectX::SimpleMath::Vector3(0.0f, height, 0.0f);

    if (playerTrans) {
        DirectX::SimpleMath::Vector3 toPlayer = playerTrans->position - drone.targetPosition;
        toPlayer.y = 0.0f;
        if (toPlayer.LengthSquared() > 0.001f) {
            toPlayer.Normalize();
            drone.targetLookDirection = toPlayer;
        }
        else {
            drone.targetLookDirection = centerDir;
        }
    }
    else {
        drone.targetLookDirection = centerDir;
    }

    // ========================================================================
    // ★ 修正: グループ別の「震え（予備動作）」ステート自動切り替えロジック
    // ========================================================================
    bool isMyTurnFirst = (isUpperFirst == isMySegmentUpper);

    if (isMyTurnFirst) {
        // 【 先攻グループ 】
        // 攻撃開始は 1.4s。その直前 (1.9s ～ 1.4s) で震える。
        if (drone.stateTimer > 1.4f && drone.stateTimer <= 1.9f) {
            drone.currentState = DroneState::Shaking;
        }
        else if (drone.stateTimer <= 1.4f && drone.stateTimer > 0.0f) {
            drone.currentState = DroneState::MoveToTarget; // スイング実行
        }
        else if (drone.stateTimer > 1.9f) {
            drone.currentState = DroneState::MoveToTarget; // 初期展開中は普通に移動
        }
    }
    else {
        // 【 後攻グループ 】
        // 攻撃開始は 1.0s。その直前 (1.5s ～ 1.0s) で震える。
        // 先攻がスイングしている最中に、後攻が震え始めるという絶妙な連携になる。
        if (drone.stateTimer > 1.0f && drone.stateTimer <= 1.5f) {
            drone.currentState = DroneState::Shaking;
        }
        else if (drone.stateTimer <= 1.0f && drone.stateTimer > 0.0f) {
            drone.currentState = DroneState::MoveToTarget; // スイング実行
        }
        else if (drone.stateTimer > 1.5f) {
            drone.currentState = DroneState::MoveToTarget; // 初期展開中や待機中は普通に移動
        }
    }
}


// ============================================================================
// メガ・シールドチャージ (Mega Shield Charge) : 上下時間差で壁から一斉突撃
// ============================================================================
inline void DroneFormationSystem::CalculateMegaShieldCharge(DroneComponent& drone, const TransformComponent& bossTrans, const BossCommandComponent& bossCmd, float dt, bool isUpperFirst) {

    // ★ 筋肉(Movement)にバトンを渡した後は、Formation側はタイマー減算を含めて一切関与しない
    if (drone.currentState == DroneState::HorizontalCharge) return;

    drone.stateTimer -= dt;

    // ------------------------------------------------------------------------
    // ★ 究極のバグ修正：Guillotine/ClapSlamの手法を完全踏襲した「空間ロックメモリ」
    // ------------------------------------------------------------------------
    // 3.5s ～ 2.5s : 展開フェーズ（プレイヤーに追従して壁の向きを変える）
    // 2.5s 以下    : 予備動作フェーズ（先攻が震え始めた瞬間に、シールドの角度と座標を永久ロック）
    // ------------------------------------------------------------------------
    bool isAttackPhase = (drone.stateTimer <= 2.5f);

    static DirectX::SimpleMath::Vector3 lockedPlayerPos = DirectX::SimpleMath::Vector3::Zero;
    static DirectX::SimpleMath::Vector3 lockedBossPos = DirectX::SimpleMath::Vector3::Zero;
    static bool isPosLocked = false;

    const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);

    if (!isAttackPhase) {
        // [展開フェーズ中] プレイヤーが生きていれば毎フレーム最新の座標をサンプリング
        if (playerTrans) {
            lockedPlayerPos = playerTrans->position;
        }
        lockedBossPos = bossTrans.position;
        isPosLocked = false; // ロックを常に解除
    }
    else {
        // [攻撃・予備動作フェーズ突入] 2.5sを切った最初の1フレームだけ、空間ターゲットを完全固定
        if (!isPosLocked) {
            if (playerTrans) {
                lockedPlayerPos = playerTrans->position;
            }
            lockedBossPos = bossTrans.position;
            isPosLocked = true; // 空間を永久ロック（以降プレイヤーの移動を完全遮断）
        }
    }

    // ロックされた確定位置を元にベクトルを算出
    Vector3 toPlayer = lockedPlayerPos - lockedBossPos;
    toPlayer.y = 0.0f;
    if (toPlayer.LengthSquared() > 0.001f) {
        toPlayer.Normalize();
    }
    else {
        // フェイルセーフ：万が一の場合はボスの正面
        toPlayer = DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3::UnitZ, bossTrans.rotation);
    }
    Vector3 right = toPlayer.Cross(Vector3::UnitY);

    int maxColumns = 20;
    float spacingX = 2.0f;
    float spacingY = 1.6f;
    float forwardOffset = 4.5f;

    if (drone.totalDrones < 64) {
        maxColumns = 6;
        spacingX = 1.8f;
        spacingY = 1.5f;
        forwardOffset = 3.0f;
    }

    // ★ボスが動いても壁が空中に固定されるように、ロックされたボスの位置を使う
    Vector3 shieldCenter = lockedBossPos + toPlayer * forwardOffset;

    int row = drone.localIndex / maxColumns;
    int col = drone.localIndex % maxColumns;

    int dronesInThisRow = (std::min)(maxColumns, drone.totalDrones - row * maxColumns);
    float wallWidth = (dronesInThisRow - 1) * spacingX;
    float startOffset = -wallWidth * 0.5f;

    float currentOffsetX = startOffset + col * spacingX;
    float currentOffsetY = row * spacingY;

    drone.targetPosition = shieldCenter + right * currentOffsetX + Vector3(0, 1.0f + currentOffsetY, 0);
    drone.targetLookDirection = toPlayer;

    // ------------------------------------------------------------------------
    // 3. タイムライン制御 (Movementの既存タメ機能を悪用した時間差連携)
    // ------------------------------------------------------------------------
    int numRows = (drone.totalDrones + maxColumns - 1) / maxColumns;
    bool isMySegmentUpper = (row >= numRows / 2);
    bool isMyTurnFirst = (isUpperFirst == isMySegmentUpper);

    // 【全体演出 3.5s】
    if (isMyTurnFirst) {
        // 先攻グループ：3.5s ～ 2.5s までは展開移動
        if (drone.stateTimer <= 2.5f) {
            // 残り2.5sを切った瞬間にMovementへ突撃指示を出し、
            // 「0.5秒間震えろ」とタイマーを上書きして権限を委譲する
            drone.currentState = DroneState::HorizontalCharge;
            drone.stateTimer = 0.5f;
            drone.fireDirection = toPlayer; // ロックされた方向へ射出
        }
        else {
            drone.currentState = DroneState::MoveToTarget;
        }
    }
    else {
        // 後攻グループ：3.5s ～ 1.5s までは展開移動＆待機
        if (drone.stateTimer <= 1.5f) {
            // 先攻が撃ち出された後（残り1.5s）の瞬間にMovementへ突撃指示を出し、
            // 「0.5秒間震えろ」とタイマーを上書きして権限を委譲する
            drone.currentState = DroneState::HorizontalCharge;
            drone.stateTimer = 0.5f;
            drone.fireDirection = toPlayer; // ロックされた方向へ射出
        }
        else {
            drone.currentState = DroneState::MoveToTarget;
        }
    }
}

// 物理の更新より前に座標を計算するため、PrePhysicsを指定
REGISTER_LOGIC_SYSTEM(DroneFormationSystem, Priority::LogicStage::L03_PrePhysics);