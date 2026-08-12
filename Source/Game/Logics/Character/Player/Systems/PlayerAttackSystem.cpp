#include "PlayerAttackSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Transform/Components/TransformComponent.h" // 位置から敵方向を求める
#include "Game/Logics/Combat/HealthComponent.h"             // 敵判定（生存中の敵）
#include "Game/Logics/Combat/DeadTag.h"                     // 死亡除外
#include "Game/Logics/Character/Player/Components/CleaveComponent.h" // 切り払い準備中は攻撃を抑止

#include <SimpleMath.h>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// トリガーのハッシュ値を事前計算
static const uint32_t HASH_TRIG_ATTACK      = CCL::Utils::HashString("Attack");
static const uint32_t HASH_TRIG_DASH_ATTACK = CCL::Utils::HashString("DashAttack"); // ダッシュ攻撃用のハッシュを追加
static const uint32_t HASH_TRIG_AIR_ATTACK  = CCL::Utils::HashString("AirAttack");  // 空中攻撃用のハッシュ

namespace
{
// --- オートフェイシングのチューニング（定数のみ：変更頻度が低いため file-scope に置く） ---
constexpr float kFaceDuration = 0.22f;              ///< 攻撃時に敵へ回頭させる時間(秒)
const     float kFaceConeCos  = std::cos(1.221730f); ///< 前方±70°の内積閾値（cos70°）
//   ※自動フェイシングの最大距離は PlayerComponent.attackFaceMaxDistance（エディタ調整可）を使う。
}

EntityID PlayerAttackSystem::FindBestFrontEnemy(EntityID selfID, const Vector3 &selfPos,
                                                const Vector3 &forward, float range, float coneCos)
{
    EntityID best       = InvalidEntityID;
    float    bestDistSq = range * range;

    for (EntityID id : m_world->View<HealthComponent, TransformComponent>())
    {
        if (id == selfID) continue;
        if (m_world->HasComponent<PlayerComponent>(id)) continue; // 味方（プレイヤー）は除外
        if (m_world->HasComponent<Tag::DeadTag>(id)) continue;

        const auto *hp = m_world->GetComponent<HealthComponent>(id);
        if (hp && hp->currentHealth <= 0.0f) continue;
        const auto *tr = m_world->GetComponent<TransformComponent>(id);
        if (!tr) continue;

        Vector3 to = Vector3(tr->position) - selfPos;
        to.y       = 0.0f;
        const float distSq = to.LengthSquared();
        if (distSq < 1e-4f || distSq > bestDistSq) continue;

        Vector3 dir = to;
        dir.Normalize();
        if (dir.Dot(forward) < coneCos) continue; // 前方コーンの外は無視

        bestDistSq = distSq; // より近い敵を採用
        best       = id;
    }
    return best;
}

void PlayerAttackSystem::SetAttackFacing(EntityID playerID, PlayerComponent &player)
{
    const auto *ptrans = m_world->GetComponent<TransformComponent>(playerID);
    if (!ptrans) return;
    const Vector3 playerPos(ptrans->position);

    // 1. 対象決定：ロックオン中の敵を最優先。無ければ前方最寄りの敵。
    EntityID target = InvalidEntityID;
    if (player.lockOnTargetID != InvalidEntityID &&
        m_world->IsEntityValid(player.lockOnTargetID) &&
        !m_world->HasComponent<Tag::DeadTag>(player.lockOnTargetID))
    {
        target = player.lockOnTargetID;
    }
    else
    {
        const Vector3 forward(std::sin(player.currentYaw), 0.0f, std::cos(player.currentYaw));
        target = FindBestFrontEnemy(playerID, playerPos, forward, player.attackFaceMaxDistance, kFaceConeCos);
    }
    if (target == InvalidEntityID) return; // 敵が居なければ向いてる方向のまま攻撃

    // 2. 対象方向の yaw を求めて回頭を予約（実際の回頭は PlayerMoveSystem が担う）。
    const auto *etrans = m_world->GetComponent<TransformComponent>(target);
    if (!etrans) return;
    Vector3 to = Vector3(etrans->position) - playerPos;
    to.y       = 0.0f;
    if (to.LengthSquared() < 1e-4f) return;

    // 近いときだけ向く：対象が attackFaceMaxDistance より遠ければ回頭しない。
    //   ロックオン中でも遠い敵には勝手に向かず、入力方向のまま攻撃できる（遠距離での暴発防止）。
    if (to.LengthSquared() > player.attackFaceMaxDistance * player.attackFaceMaxDistance) return;

    player.attackFaceYaw   = std::atan2(to.x, to.z); // PlayerMoveSystem と同じ atan2(x,z) 規約
    player.attackFaceTimer = kFaceDuration;
}

void PlayerAttackSystem::Update(float dt)
{
    // 引数に DashComponent を追加（宣言順に合わせる）
    ForEachWithID([&](EntityID playerID,
                const CharacterMovementInputComponent &inputData,
                DashComponent                         &dash,
                AnimParametersComponent               &animParams,
                PlayerComponent                       &player) {
        // 切り払いの準備(Ready)/突進(Lunging)中は、左クリックを「切り払いの発動」に使う。
        // ここで通常/ダッシュ/空中攻撃を出さないよう抑止し、エッジ状態だけ更新して抜ける。
        if (const auto *cleave = m_world->GetComponent<CleaveComponent>(playerID))
        {
            if (cleave->phase != CleaveComponent::Phase::Idle)
            {
                player.wasAttackButtonPressed = inputData.isAttackRequested;
                return;
            }
        }

        // 前フレームで押されておらず、今フレームで押された瞬間（エッジ検出）
        if (inputData.isAttackRequested && !player.wasAttackButtonPressed)
        {
            // 攻撃の瞬間に対象へ回頭を予約（地上/空中どちらも）。
            SetAttackFacing(playerID, player);

            // ==============================================================
            // 空中攻撃の判定（最優先）
            //   空中にいる間に攻撃したら、地上の通常/ダッシュ攻撃ではなく空中攻撃を出す。
            //   実際のアニメーション遷移・攻撃判定は「AirAttack」トリガーを受けて別途実装する。
            // ==============================================================
            if (player.isAirborne)
            {
                animParams.SetTrigger(HASH_TRIG_AIR_ATTACK, 0.25f);
                CCL_LOG_INFO(LogCategory::Game, "Air Attack Triggered!");
            }
            else
            {
                // ==============================================================
                // ダッシュ攻撃の判定 (速度・慣性ベース)
                // ==============================================================
                // 閾値: ダッシュ最高速度の 50% 以上のスピードが出ている時とする
                float thresholdSpeed = dash.dashSpeed * 0.5f;
                float currentSpeedSq = inputData.currentVelocity.LengthSquared();

                // 走行中(isRunning)／回避バースト中(isDashing)は常にダッシュ攻撃。
                // 加えて慣性で十分な速度が残っている場合も許可（走行解除直後の猶予）。
                if (dash.isDashing || player.isRunning || currentSpeedSq >= (thresholdSpeed * thresholdSpeed))
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
        }

        // 次のフレームのエッジ検出のために状態を保存
        player.wasAttackButtonPressed = inputData.isAttackRequested;
    });
}

// 実行タイミング：PlayerInputSystem(L01_Input)の直後、AnimControllerSystem(L02_Update)より前
REGISTER_LOGIC_SYSTEM(PlayerAttackSystem, Priority::LogicStage::L01_Input);
