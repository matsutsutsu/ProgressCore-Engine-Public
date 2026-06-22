#include "PlayerDashSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerDashSystem::Update(float rawDt)
{
    ForEach([&](CharacterMovementInputComponent &input,
                DashComponent                   &dash,
                const TransformComponent        &trans,
                const TimeStateComponent        &time) {
        // 1. クールタイムの減算
        if (!dash.isDashing && dash.currentTimer > 0.0f)
        {
            dash.currentTimer -= time.localDt;
        }

        // 2. Dash開始処理
        if (input.dashRequested && !dash.isDashing && dash.currentTimer <= 0.0f)
        {
            dash.isDashing    = true;
            dash.isInvincible = true;
            dash.currentTimer = dash.dashDuration;

            // 移動入力があればその方向、無入力ならキャラの正面方向へダッシュ
            if (input.desiredVelocity.LengthSquared() > 0.001f)
            {
                dash.dashDirection = input.desiredVelocity;
            }
            else
            {
                Vector3 forward    = Vector3::Transform(Vector3::UnitZ, trans.rotation);
                forward.y          = 0.0f;
                dash.dashDirection = (forward.LengthSquared() > 0.001f) ? forward : Vector3::UnitZ;
            }
            dash.dashDirection.Normalize();
        }

        // 3. Dash中の状態更新と速度の注入
        if (dash.isDashing)
        {
            dash.currentTimer -= time.localDt;

            if (dash.currentTimer <= 0.0f)
            {
                // Dash終了 (クールダウンへ移行)
                dash.isDashing      = false;
                dash.isInvincible   = false;
                dash.currentTimer   = dash.dashCooldown;
                input.overrideSpeed = -1.0f; // 速度上書きを解除
            }
            else
            {
                // Dash実行中：方向と速度を強制上書きする
                input.desiredVelocity = dash.dashDirection;
                input.overrideSpeed   = dash.dashSpeed;
            }
        }
        else
        {
            // Dashしていない時は通常速度で動かすよう指示
            input.overrideSpeed = -1.0f;
        }

        // 次のフレームのために要求フラグを落とす
        input.dashRequested = false;
    });
}

// MoveSystemで振り向かせる前(または同タイミング)に実行
REGISTER_LOGIC_SYSTEM(PlayerDashSystem, Priority::LogicStage::L02_Update);