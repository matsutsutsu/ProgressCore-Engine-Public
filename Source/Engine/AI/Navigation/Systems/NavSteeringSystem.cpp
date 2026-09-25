#include "NavSteeringSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "SimpleMath.h"
#include <algorithm>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace DirectX::SimpleMath;
using namespace CCL::ECS;

// 読み書きするデータ:
//   WRITE : TransformComponent（向き。位置は物理が動かす）
//   WRITE : NavAgentComponent（currentWaypointIndex の更新）
//   WRITE : CharacterMovementComponent（物理へ渡す移動速度）
void NavSteeringSystem::Update(float dt)
{
    ForEachWithID([&](EntityID                    entity,
                      TransformComponent         &transform,
                      NavAgentComponent          &navAgent,
                      CharacterMovementComponent &movement) {
        // 1. 経路が無い、または最後まで着いたなら止める
        if (navAgent.waypoints.empty() || navAgent.currentWaypointIndex >= navAgent.waypoints.size())
        {
            movement.desiredVelocity = Vector3::Zero;
            return;
        }

        Vector3 currentPos = transform.position;
        Vector3 targetPos  = navAgent.waypoints[navAgent.currentWaypointIndex];

        // 2. 今の waypoint への方向。高さの差で斜めに進もうとしないよう、水平成分だけにする
        Vector3 dirToTarget = targetPos - currentPos;
        dirToTarget.y       = 0.0f;

        float distToTarget = dirToTarget.Length();

        // waypoint に着いたかの判定もここで行う
        if (distToTarget <= navAgent.stopDistance)
        {
            // 次の目印へターゲットを切り替える
            navAgent.currentWaypointIndex++;

            // このステップは速度を更新せず、次のステップで新しい目標に向かって計算する
            return;
        }

        // 3. 速度は AI が NavAgentComponent に書いた値を使う
        float speed = navAgent.currentDesiredSpeed;

        // 4. 物理（キャラクターコントローラー）へ渡す速度を作る
        if (distToTarget > 0.001f && speed > 0.0f)
        {
            dirToTarget.Normalize();
            movement.desiredVelocity = dirToTarget * speed;

            // 進行方向へ滑らかに向きを変える（Y 軸回転）。
            // atan2f で X と Z から向くべき角度（ヨー）を求める
            float      targetYaw = atan2f(dirToTarget.x, dirToTarget.z);
            Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);

            // dt が大きいと係数が 1.0 を超えて行き過ぎるので、1.0 で止める
            float slerpAmount  = (std::min)(10.0f * dt, 1.0f);
            transform.rotation = Quaternion::Slerp(transform.rotation, targetRot, slerpAmount);
        }
        else
        {
            movement.desiredVelocity = Vector3::Zero;
        }
    });
}

// TODO(2026-07-11): 移動方針（Nav）を実装するときに REGISTER_LOGIC_SYSTEM で登録し直す。
//   以前の登録先ステージ（L02_AI_Act）は廃止済み。NavPathfindingSystem の後、
//   物理（L04）より前のステージに置くこと