#include "PlayerDashSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Game/Logics/AI/Battle/BattleContext.h"

#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    // 接近を終了し、プレイヤーの時計を通常へ戻す共通処理。
    void EndApproach(DashComponent &dash, CharacterMovementInputComponent &input, TimeStateComponent &time)
    {
        dash.isApproaching  = false;
        dash.isInvincible   = false;
        dash.approachTimer  = 0.0f;
        dash.currentTimer   = dash.dashCooldown; // 接近後はダッシュのクールダウンへ
        time.cinematicScale = 1.0f;              // 世界スロー打ち消しを解除（通常の時間へ復帰）
        input.overrideSpeed = -1.0f;
    }
}

void PlayerDashSystem::Update(float rawDt)
{
    // 接近先(ボス)と世界スロー倍率をフレーム先頭で拾っておく（1対1なので探索不要）。
    const BattleContext *battle  = _world->HasResource<BattleContext>() ? &_world->GetResource<BattleContext>() : nullptr;
    const TimeContext   *timeCtx = _world->HasResource<TimeContext>() ? &_world->GetResource<TimeContext>() : nullptr;

    ForEach([&](CharacterMovementInputComponent &input,
                DashComponent                   &dash,
                const TransformComponent        &trans,
                TimeStateComponent              &time) {
        // ============================================================
        // 0. 接近（ジャスト回避スロー中にボスへ詰める「接近専用時間」）
        //    通常ダッシュより優先。企画: 殴れる位置に着くための時間であり、
        //    世界はスローのままプレイヤーだけ実時間で高速に寄る。
        // ============================================================
        if (dash.isApproaching)
        {
            input.dashRequested = false; // 接近中はダッシュ入力を握り潰す

            // 接近先(ボス)が居なければ打ち切る
            if (!battle || battle->bossEntity == InvalidEntityID)
            {
                EndApproach(dash, input, time);
                return;
            }

            // ヒットストップ中（ジャスト回避直後の一瞬の停止＝溜め）は待機。
            // 明けてから接近を開始することで「停止→スリングショット」の手触りを作る。
            if (time.hitStopTimer > 0.0f)
                return;

            // ボスへの水平ベクトル
            Vector3 toBoss(battle->bossPos.x - trans.position.x,
                           0.0f,
                           battle->bossPos.z - trans.position.z);
            float dist = toBoss.Length();

            // 時間切れ、または殴れる距離まで詰めたら終了
            dash.approachTimer -= rawDt;
            if (dash.approachTimer <= 0.0f || dist <= dash.approachStopRange)
            {
                EndApproach(dash, input, time);
                return;
            }

            // ボス方向へ高速移動。接近中は無敵でスロー中の群れをすり抜ける。
            toBoss.Normalize();
            dash.dashDirection    = toBoss;
            dash.isInvincible     = true;
            input.desiredVelocity = toBoss;
            input.overrideSpeed   = dash.approachSpeed;

            // 世界スロー(globalScale × 勢力倍率)を打ち消し、プレイヤーだけ実効時間で動かす。
            if (timeCtx)
            {
                float worldScale = timeCtx->globalScale
                                 * timeCtx->groupScales[static_cast<size_t>(time.group)];
                float safe = (worldScale > 0.0001f) ? worldScale : 0.0001f;
                time.cinematicScale = dash.approachPlayerScale / safe;
            }
            return; // 接近中は通常ダッシュ処理をスキップ
        }

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
            dash.isJustEvadeWindowActive = true; // ダッシュ開始と同時にジャスト受付開始
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

            // ダッシュ開始から指定時間(justEvadeWindow)が経過したら、ジャスト受付のみ終了する
            float elapsedTime = dash.dashDuration - dash.currentTimer;
            if (elapsedTime > dash.justEvadeWindow)
            {
                dash.isJustEvadeWindowActive = false;
            }

            if (dash.currentTimer <= 0.0f)
            {
                // Dash終了 (クールダウンへ移行)
                dash.isDashing               = false;
                dash.isInvincible            = false;
                dash.isJustEvadeWindowActive = false; // 念のため
                dash.currentTimer            = dash.dashCooldown;
                input.overrideSpeed          = -1.0f;
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