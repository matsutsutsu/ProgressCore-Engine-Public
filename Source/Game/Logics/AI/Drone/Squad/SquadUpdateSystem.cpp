#include "SquadUpdateSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kTwoPi = 6.28318530f;
}

void SquadUpdateSystem::Initialize()
{
    EnsureRegistry();
}

void SquadUpdateSystem::EnsureRegistry()
{
    if (m_world->HasResource<SquadRegistry>()) return;
    m_world->AddResource<SquadRegistry>();
}

void SquadUpdateSystem::Update(float dt)
{
    EnsureRegistry();
    auto &reg = m_world->GetResource<SquadRegistry>();

    m_elapsed += dt;

    // ボスの命令と位置（1v1 なのでボスは1体）。
    DroneFormationType formation = DroneFormationType::Hidden;
    bool               attacking = false;
    Vector3            bossPos   = Vector3::Zero;
    {
        auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
        for (auto b : bossView)
        {
            if (const auto *cmd = m_world->GetComponent<BossCommandComponent>(b))
            {
                formation = cmd->requestFormation;
                attacking = cmd->droneAttackActive;
            }
            if (const auto *t = m_world->GetComponent<TransformComponent>(b))
                bossPos = Vector3(t->GetWorldPosition()); // position はローカル
            break;
        }
    }

    // プレイヤー不在時は接近も突入も目標が無い。原点へ向かわせないよう、
    // 座標を取れたかどうかを持ち回る。
    Vector3 playerPos = Vector3::Zero;
    bool    hasPlayer = false;
    if (m_world->HasResource<BattleContext>())
    {
        const auto &battle = m_world->GetResource<BattleContext>();
        if (battle.HasPlayer())
        {
            playerPos = Vector3(battle.playerPos);
            hasPlayer = true;
        }
    }

    // 生存機数と各席の実位置を毎フレーム取り直す（撃破で小隊が痩せるため）。
    // 実位置は「追従機が自分の前の機体を追う」ために要る。
    for (auto &s : reg.squads)
    {
        s.aliveCount = 0;
        s.memberValid.fill(false);
    }

    ForEach([&](const DroneComponent &drone, const TransformComponent &trans) {
        if (drone.squadId >= SquadRegistry::kMaxSquads) return; // 無所属（大技用）
        SquadState &s = reg.squads[drone.squadId];
        if (s.aliveCount < 255) ++s.aliveCount;

        if (drone.slotInSquad < SquadRegistry::kSquadSize)
        {
            s.memberPos[drone.slotInSquad]   = Vector3(trans.position);
            s.memberVel[drone.slotInSquad]   = drone.currentVelocity;
            s.memberValid[drone.slotInSquad] = true;
        }
    });

    // 小隊の数だけ回す（ドローンの数ではない）。
    //   プレイヤーが居なければ小隊行動そのものを解除する（Regroup へ落ちる）。
    const bool ordered = hasPlayer && (formation == DroneFormationType::SquadAssault);

    for (uint8_t i = 0; i < SquadRegistry::kMaxSquads; ++i)
    {
        SquadState &s = reg.squads[i];
        if (s.aliveCount == 0) continue;
        UpdateSquad(s, i, ordered, attacking, bossPos, playerPos, reg.tuning, dt);
    }
}

void SquadUpdateSystem::UpdateSquad(SquadState        &squad,
                                    uint8_t            squadId,
                                    bool               ordered,
                                    bool               attacking,
                                    const Vector3     &bossPos,
                                    const Vector3     &playerPos,
                                    const SquadTuning &tuning,
                                    float              dt)
{
    if (squad.phaseTimer > 0.0f) squad.phaseTimer -= dt;

    // フェーズ遷移（優先度順。上から最初に成立したものを採る）。
    SquadPhase next;
    if (ordered == false)
    {
        next = SquadPhase::Regroup; // 別の陣形を指示された＝小隊行動は解除
    }
    else if (squad.phase == SquadPhase::Strike)
    {
        // 突入は始まったら時間で必ず終わる。信号が落ちた瞬間に急停止すると「突っ込んで
        // きたのに止まる」ので通り抜けるまで走らせ、上がりっぱなしでも時間で切り上げる。
        next = (squad.phaseTimer > 0.0f) ? SquadPhase::Strike : SquadPhase::Approach;
    }
    else if (attacking && squad.phaseTimer <= 0.0f)
    {
        // 接近の最低時間を過ぎるまで突入しない＝予備動作を読ませる窓を必ず作る。
        next = SquadPhase::Strike;
    }
    else
    {
        next = SquadPhase::Approach;
    }

    if (next != squad.phase)
    {
        const SquadPhase prev = squad.phase;
        squad.phase           = next;

        if (next == SquadPhase::Strike)
        {
            squad.phaseTimer = tuning.strikeDuration;
        }
        else if (prev == SquadPhase::Strike)
        {
            // 突入直後だけ間隔を空ける（連続体当たりの防止）。
            squad.phaseTimer = tuning.approachMinTime;
        }
        else
        {
            // 隊列を組んだ直後は即座に突入できる状態にする。予備動作の長さは BT の Wait が
            // 決めるので、ここでも待たせると待ち時間が二重になり、BT の想定とずれる。
            squad.phaseTimer = 0.0f;
        }
    }

    // 突入: アンカーがプレイヤーを追い、後続機はそれに付いていく。
    if (squad.phase == SquadPhase::Strike)
    {
        SteerTowardPlayer(squad, playerPos, tuning, dt);
        squad.anchorVelocity = squad.anchorForward * tuning.strikeSpeed;
        squad.anchorPos      = squad.anchorPos + squad.anchorVelocity * dt;
        ClampAltitude(squad, tuning);
        return;
    }

    // 待機／接近：目標点をバネで追う。小隊ごとに角度をずらすので、複数小隊が
    // 別方向から包囲する形に自然になる。
    const float phaseAngle = (kTwoPi * static_cast<float>(squadId)) /
                             static_cast<float>(SquadRegistry::kMaxSquads);

    Vector3 desired;
    if (squad.phase == SquadPhase::Approach)
    {
        // 斜め横から回り込み、側面から迫られるように見せる。
        const float a = phaseAngle + m_elapsed * kApproachDrift;
        desired       = playerPos + Vector3(std::cos(a) * tuning.approachRadius,
                                            tuning.approachHeight,
                                            std::sin(a) * tuning.approachRadius);
    }
    else
    {
        const float a = phaseAngle + m_elapsed * 0.25f;
        desired       = bossPos + Vector3(std::cos(a) * kRegroupRadius,
                                          kRegroupHeight,
                                          std::sin(a) * kRegroupRadius);
    }

    // 初回だけ目標へ瞬間移動させる（ワールド原点から飛んでくるのを防ぐ）。
    if (squad.initialized == false)
    {
        squad.initialized    = true;
        squad.anchorPos      = desired;
        squad.anchorVelocity = Vector3::Zero;
    }

    Vector3 targetVel = (desired - squad.anchorPos) * kAnchorGain;
    if (targetVel.LengthSquared() > kCruiseSpeed * kCruiseSpeed)
    {
        targetVel.Normalize();
        targetVel *= kCruiseSpeed;
    }
    squad.anchorVelocity = Vector3::Lerp(squad.anchorVelocity, targetVel,
                                         (std::min)(kAnchorDamping * dt, 1.0f));
    squad.anchorPos      = squad.anchorPos + squad.anchorVelocity * dt;

    // 接近中はプレイヤーの方を向く（これから来ると読ませる）。待機中は進行方向を向く。
    Vector3 face = squad.anchorVelocity;
    if (squad.phase == SquadPhase::Approach)
    {
        face = Vector3(playerPos.x - squad.anchorPos.x, 0.0f, playerPos.z - squad.anchorPos.z);
    }

    if (face.LengthSquared() > 0.001f)
    {
        face.Normalize();
        Vector3 blended = Vector3::Lerp(squad.anchorForward, face, (std::min)(4.0f * dt, 1.0f));
        if (blended.LengthSquared() > 0.001f)
        {
            blended.Normalize();
            squad.anchorForward = blended;
        }
    }

    ClampAltitude(squad, tuning);
}

void SquadUpdateSystem::ClampAltitude(SquadState &squad, const SquadTuning &tuning) const
{
    const float floorY = kSquadGroundY + tuning.minAltitude;
    if (squad.anchorPos.y >= floorY) return;

    squad.anchorPos.y = floorY;

    // 位置を止めるだけでは直らない。速度が下を向いたままだと、追従機がそれを
    // followFeedForward として受け取って床へ押し込まれ続ける。
    if (squad.anchorVelocity.y < 0.0f) squad.anchorVelocity.y = 0.0f;

    // 向きも水平にする。真下を向いたまま突入速度を掛けると、速度の大半が止められた
    // 縦方向に消えて床に張り付く。水平成分だけ残せば床の近くを飛んで通り過ぎる。
    if (squad.anchorForward.y < 0.0f)
    {
        squad.anchorForward.y = 0.0f;
        if (squad.anchorForward.LengthSquared() < 0.001f)
            squad.anchorForward = Vector3::UnitZ; // 真下を向いていた場合の退避
        squad.anchorForward.Normalize();
    }
}

void SquadUpdateSystem::SteerTowardPlayer(SquadState        &squad,
                                          const Vector3     &playerPos,
                                          const SquadTuning &tuning,
                                          float              dt) const
{
    Vector3     toPlayer = playerPos - squad.anchorPos;
    const float dist     = toPlayer.Length();

    // 終末ロック。突入の最後を直線に固定するので、読んで避ける時間が必ず残る
    // （完全追尾にすると、避けても曲がって当たり、ジャスト回避が成立しない）。
    if (dist <= tuning.terminalLockDist || dist < 0.001f) return;

    toPlayer /= dist;

    // 1フレームで曲がれる角度に上限を掛ける。
    const float maxRad = DirectX::XMConvertToRadians(tuning.maxTurnRateDeg) * dt;
    const float cosA   = std::clamp(squad.anchorForward.Dot(toPlayer), -1.0f, 1.0f);
    const float angle  = std::acos(cosA);

    if (angle <= 1.0e-4f)
    {
        squad.anchorForward = toPlayer;
        return;
    }

    const float t       = (std::min)(maxRad / angle, 1.0f);
    Vector3     blended = Vector3::Lerp(squad.anchorForward, toPlayer, t);
    if (blended.LengthSquared() > 0.001f)
    {
        blended.Normalize();
        squad.anchorForward = blended;
    }
}

// DroneCommandSystem の後、DroneFormationSystem の前（前後は両者の GetRunAfter で決める）。
REGISTER_LOGIC_SYSTEM(SquadUpdateSystem, Priority::LogicStage::L02_Update)
