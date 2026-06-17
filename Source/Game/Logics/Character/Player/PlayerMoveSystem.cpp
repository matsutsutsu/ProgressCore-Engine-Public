#include "PlayerMoveSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerMoveSystem::Update(float rawDt)
{
    ForEachWithID([&](EntityID id,
        TransformComponent& trans,
        const CharacterMovementInputComponent& inputData,
        const PlayerComponent& player,
        const TimeStateComponent& time)
        {
            // 死亡判定（タグがあれば処理スキップ）
            if (_world->HasComponent<Tag::DeadTag>(id)) return;          

            // =========================================================
            // 1. 移動の適用 (今回はTransformへ直接加算する簡易実装とする。
            //    Jolt Physics 等が後段にあるなら、ここは不要になる可能性がある)
            // =========================================================
            if (inputData.desiredVelocity.LengthSquared() > 0.001f) {
                // ★ ガイドライン準拠: rawDt ではなく必ず time.localDt を使用する
                trans.position = trans.position + inputData.desiredVelocity * time.localDt;
                trans.isStatic = false;
            }

            // =========================================================
            // 2. 振り向き（マウスカーソル位置へのエイム）
            // =========================================================
            Vector3 toTarget = inputData.lookTargetPos - trans.position;
            toTarget.y = 0.0f; // トップビューのためY軸(高さ)の差分は無視する

            if (toTarget.LengthSquared() > 0.001f) {
                toTarget.Normalize();

                // XとZからヨー角(Yaw)を計算
                float targetYaw = atan2f(toTarget.x, toTarget.z);
                Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);

                // ★ ガイドライン準拠: 回転の補間にも必ず time.localDt を使用する
                trans.rotation = Quaternion::Slerp(trans.rotation, targetRot, player.turnSpeed * time.localDt);

                // 物理エンジンやSIMD命令に不正な数値を送らないための正規化保護
                DirectX::XMVector4Normalize(DirectX::XMLoadFloat4(&trans.rotation));
                trans.isStatic = false;
            }
        });
}

// ステージ L02: 入力が確定した後、物理エンジンの前に実行する
REGISTER_LOGIC_SYSTEM(PlayerMoveSystem, Priority::LogicStage::L02_Update);