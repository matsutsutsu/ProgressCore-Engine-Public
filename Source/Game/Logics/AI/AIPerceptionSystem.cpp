#include "AIPerceptionSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Physics/IPhysicsAPI.h"
#include "Game/Logics/Character/PlayerCar/PlayerCarComponent.h"
#include "Engine/Platform/Logger.h"

// ロジックの可読性を上げるための SimpleMath
#include "SimpleMath.h"
using namespace DirectX::SimpleMath;
using namespace CCL::ECS;

// システム登録用マクロ
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"


void AIPerceptionSystem::Update(float dt) {
    if (!_world || !_world->HasResource<std::shared_ptr<IPhysicsAPI>>()) return;
    auto physics = _world->GetResource<std::shared_ptr<IPhysicsAPI>>();
    if (!physics) return;

    // 1. プレイヤー（追跡対象）の検索
    EntityID playerEntity = InvalidEntityID;
    Vector3 playerPos = Vector3::Zero;

    auto playerView = _world->View<TransformComponent, PlayerCarComponent>();
    for (auto p : playerView) {
        playerEntity = p;
        playerPos = _world->GetComponent<TransformComponent>(p)->position;
        break;
    }

    if (playerEntity == InvalidEntityID) return;

    // 2. AIの視覚判定（ForEachを使用して並列・高速に処理）
    ForEachWithID([&](EntityID aiEntity,
        const TransformComponent& transform,
        const AIPerceptionComponent& perception,
        AIMemoryComponent& memory,
        AIStateComponent& state,
        NavAgentComponent& navAgent) {

            if (perception.visionRange <= 0.0f) return;

            Vector3 aiPos = transform.position;
            Vector3 toPlayer = playerPos - aiPos;
            float distToPlayer = toPlayer.Length();

            bool canSeePlayer = false;

            // ① 距離チェック
            if (distToPlayer <= perception.visionRange && distToPlayer > 0.001f) {

                // ② 角度チェック (SimpleMath::Matrix を活用)
                Matrix worldMat = transform.worldMatrix;
                Vector3 forward = worldMat.Forward(); // 行列から正面方向を直感的に取得
                forward.Normalize();

                Vector3 dirToPlayer = toPlayer;
                dirToPlayer.Normalize();

                float dot = forward.Dot(dirToPlayer);
                float angleDeg = DirectX::XMConvertToDegrees(acosf(std::clamp(dot, -1.0f, 1.0f)));

                if (angleDeg <= perception.visionAngle) {
                    // ③ 遮蔽物チェック (Jolt Raycast)
                    // 視点を足元から少し上げる（1.5m）
                    Vector3 rayStart = aiPos + Vector3(0, 1.5f, 0);

                    PhysicsHitInfo hit = physics->RayCast(rayStart, dirToPlayer, perception.visionRange, aiEntity);

                    // ヒットしない、またはプレイヤーに当たった場合
                    if (!hit.hit || hit.entity == playerEntity || hit.distance >= distToPlayer) {
                        canSeePlayer = true;
                    }
                }
            }

            // ---------------------------------------------------------
            // 🧠 状態遷移と記憶の更新
            // ---------------------------------------------------------
            if (canSeePlayer) {
                memory.lastKnownPlayerPos = playerPos;
                memory.suspicionLevel = 100.0f;

                if (state.currentState != AIState::Chase) {
                    state.currentState = AIState::Chase;
                    state.timeInState = 0.0f;
                    CCL_LOG_INFO(LogCategory::Core, "AI[%llu]: Target Spotted!", aiEntity);
                }

                // ターゲットが1m以上動いたら経路を再計算
                float moveDelta = Vector3::Distance(playerPos, navAgent.targetPosition);
                if (moveDelta > 1.0f || navAgent.waypoints.empty()) {
                    navAgent.targetPosition = playerPos;
                    navAgent.isPathRequested = true;
                }
            }
            else {
                // 見失った瞬間、最後に見た場所を調査するモードへ
                if (state.currentState == AIState::Chase) {
                    state.currentState = AIState::Investigate;
                    state.timeInState = 0.0f;

                    navAgent.targetPosition = memory.lastKnownPlayerPos;
                    navAgent.isPathRequested = true;
                    CCL_LOG_INFO(LogCategory::Core, "AI[%llu]: Target Lost. Investigating...", aiEntity);
                }
            }
        });
}


// システムの登録：視覚(Perception)は意思決定(Patrol)よりも前に行う
REGISTER_LOGIC_SYSTEM(AIPerceptionSystem, Priority::LogicStage::L02_Update)