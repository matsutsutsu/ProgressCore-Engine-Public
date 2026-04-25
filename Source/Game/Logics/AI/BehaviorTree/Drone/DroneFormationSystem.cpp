/**
 * @file DroneFormationSystem.cpp
 * @brief ドローンフォーメーションシステムの実装
 */
#include "DroneFormationSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Game/Core/SystemPriority.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// ============================================================================
// メインの更新処理
// ============================================================================
void DroneFormationSystem::Update(float dt) {

    // システム全体での経過時間を蓄積
    m_currentTime += dt;
    float currentTime = m_currentTime;

    // 超並列処理による各ドローンの更新
    ForEachParallel([this, dt, currentTime](DroneComponent& drone) {

        // 1. 親と指示の取得（フェイルセーフ）
        const auto* bossTrans = _world->GetComponent<TransformComponent>(drone.ownerBossId);
        const auto* bossCmd = _world->GetComponent<BossCommandComponent>(drone.ownerBossId);
        if (!bossTrans || !bossCmd) return;

        // 2. ボスの指示の変更を検知してドローン状態を遷移
        HandleStateTransition(drone, *bossCmd);

        // 3. 状態に基づく目標座標の算出（ポリモルフィズムを使わずswitchで高速分岐）
        switch (drone.currentFormation) {
        case DroneFormationType::OrbitCircle:
            CalculateOrbitCircle(drone, *bossTrans, currentTime);
            break;

        case DroneFormationType::SequentialAttack:
            CalculateSequentialAttack(drone, *bossCmd, dt);
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

inline void DroneFormationSystem::HandleStateTransition(DroneComponent& drone, const BossCommandComponent& bossCmd) {
    if (drone.currentFormation != bossCmd.requestFormation) {
        drone.currentFormation = bossCmd.requestFormation;

        if (drone.currentFormation == DroneFormationType::SequentialAttack) {
            drone.currentState = DroneState::LockOn;
            // 発射タイミングを個体ごとにズラす（有機的なドミノ倒し効果）
            drone.stateTimer = drone.localIndex * 0.3f;
        }
        else {
            drone.currentState = DroneState::MoveToTarget;
        }
    }
}


// 円形フォーメーションの目標座標を計算する関数
inline void DroneFormationSystem::CalculateOrbitCircle(DroneComponent& drone, const TransformComponent& bossTrans, float currentTime) {
    // 【数式】 \theta_i = \text{base\_angle} + \left( \frac{2\pi}{N} \right) \times i
    float baseAngle = currentTime * 2.0f;
    float angleOffset = (DirectX::XM_2PI / (float)drone.totalDrones) * (float)drone.localIndex;
    float finalAngle = baseAngle + angleOffset;

    Vector3 offset;
    offset.x = cosf(finalAngle) * drone.orbitRadius;
    offset.y = 2.0f + sinf(currentTime * 3.0f + drone.localIndex) * 0.5f; // Y軸の微細な波打ち
    offset.z = sinf(finalAngle) * drone.orbitRadius;

    // ボスの相対座標から絶対座標へ変換して目標を決定
    drone.targetPosition = bossTrans.position + offset;
}


// 順番攻撃フォーメーションの目標座標と状態を計算する関数
inline void DroneFormationSystem::CalculateSequentialAttack(DroneComponent& drone, const BossCommandComponent& bossCmd, float dt) {
    // ロックオン（待機）中のみタイマーを減らし、0になったら突撃を開始する
    if (drone.currentState == DroneState::LockOn) {
        drone.stateTimer -= dt;
        if (drone.stateTimer <= 0.0f) {
            const auto* playerTrans = _world->GetComponent<TransformComponent>(bossCmd.targetPlayerId);
            if (playerTrans) {
                drone.targetPosition = playerTrans->position;
                drone.currentState = DroneState::FireCharge;
            }
        }
    }
}

// 物理の更新より前に座標を計算するため、PrePhysicsを指定
REGISTER_LOGIC_SYSTEM(DroneFormationSystem, Priority::LogicStage::L03_PrePhysics);