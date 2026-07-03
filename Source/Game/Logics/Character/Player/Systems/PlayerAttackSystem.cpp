#include "PlayerAttackSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h" // ★追加

using namespace CCL::ECS;

// トリガーのハッシュ値を事前計算
static const uint32_t HASH_TRIG_ATTACK      = CCL::Utils::HashString("Attack");
static const uint32_t HASH_TRIG_DASH_ATTACK = CCL::Utils::HashString("DashAttack"); // ★ダッシュ攻撃用のハッシュを追加

void PlayerAttackSystem::Update(float dt)
{
    // ★引数に DashComponent を追加（宣言順に合わせる）
    ForEach([&](const CharacterMovementInputComponent &inputData,
                DashComponent                         &dash,
                AnimParametersComponent               &animParams,
                PlayerComponent                       &player) {
        // 前フレームで押されておらず、今フレームで押された瞬間（エッジ検出）
        if (inputData.isAttackRequested && !player.wasAttackButtonPressed)
        {
            // ==============================================================
            // ★ ダッシュ攻撃の判定 (速度・慣性ベース)
            // ==============================================================
            // 閾値: ダッシュ最高速度の 50% 以上のスピードが出ている時とする
            float thresholdSpeed = dash.dashSpeed * 0.5f;
            float currentSpeedSq = inputData.currentVelocity.LengthSquared();

            // isDashing が true、または慣性によって十分な速度が出ている場合
            if (dash.isDashing || currentSpeedSq >= (thresholdSpeed * thresholdSpeed))
            {
                // ダッシュ攻撃のトリガーを発行
                animParams.SetTrigger(HASH_TRIG_DASH_ATTACK, 0.25f);

                // 【重要】ダッシュ中に攻撃を出した場合は、ダッシュの無敵や受付窓を強制キャンセルする
                if (dash.isDashing)
                {
                    dash.isDashing               = false;
                    dash.isInvincible            = false;
                    dash.isJustEvadeWindowActive = false;
                    // すぐに次のダッシュが出せないよう、クールダウンを発生させる
                    dash.currentTimer = dash.dashCooldown;
                }

                CCL_LOG_INFO(LogCategory::Game, "Dash Attack Triggered! SpeedSq: %.1f", currentSpeedSq);
            }
            else
            {
                // 停止時、または通常の歩き状態なら通常攻撃
                animParams.SetTrigger(HASH_TRIG_ATTACK, 0.25f);
            }
        }

        // 次のフレームのエッジ検出のために状態を保存
        player.wasAttackButtonPressed = inputData.isAttackRequested;
    });
}

// 実行タイミング：PlayerInputSystem(L01_Input)の直後、AnimControllerSystem(L02_Update)より前
REGISTER_LOGIC_SYSTEM(PlayerAttackSystem, Priority::LogicStage::L01_Input);