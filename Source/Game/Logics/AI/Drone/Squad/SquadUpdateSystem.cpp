#include "SquadUpdateSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/TimeState.h"

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kTwoPi = 6.28318530f;

    /**
     * @brief 小隊番号から -1〜1 の値を作る。小隊ごとの個体差（速さ・狙いのずれ）に使う。
     *
     * 乱数にしないのは、当たった・外れたの原因を同じ条件で再現して調整できるようにするため。
     *
     * 黄金比の小数部を使うと、番号が連続していても値が偏らずに散らばる。
     *
     * @param squadId 小隊番号。
     * @return -1〜1 の値。同じ番号からは常に同じ値が返る。
     */
    float SquadVariation(uint8_t squadId)
    {
        const float x = static_cast<float>(squadId) * 0.6180340f;
        return (x - std::floor(x)) * 2.0f - 1.0f;
    }
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

void SquadUpdateSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. 時間を進める
    // 2. ボスの命令と位置を読む
    // 3. プレイヤーの位置を読む
    // 4. 各小隊の生存数と、席ごとの位置・速度を集め直す
    // 5. 小隊ごとに段階とアンカーを更新する

    EnsureRegistry();
    auto &reg = m_world->GetResource<SquadRegistry>();

    // --- 時間 ---

    // 後続機と同じ時計で進める（DroneFormationSystem と同じ理由）。
    // rawDt のままだと、世界スロー中にアンカーだけ等速で進み、スローで追う後続機との間で隊列がちぎれる。
    float dt = rawDt;
    if (m_world->HasResource<TimeContext>())
    {
        const auto &time = m_world->GetResource<TimeContext>();
        dt *= time.globalScale * time.groupScales[static_cast<size_t>(TimeGroup::Enemy)];
    }

    m_elapsed += dt;

    // --- ボス ---

    // 1対1の戦闘なのでボスは1体。最初の1体の命令と位置を読む。
    DroneFormationType formation = DroneFormationType::None;
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

    // --- プレイヤー ---

    // プレイヤー不在時は、展開も追跡も目標が無い。
    // 原点へ向かわせないよう、座標を取れたかどうかを持ち回る。
    Vector3 playerPos      = Vector3::Zero;
    float   playerRunSpeed = 0.0f;
    bool    hasPlayer      = false;
    if (m_world->HasResource<BattleContext>())
    {
        const auto &battle = m_world->GetResource<BattleContext>();
        if (battle.HasPlayer())
        {
            playerPos      = Vector3(battle.playerPos);
            playerRunSpeed = battle.playerRunSpeed;
            hasPlayer      = true;
        }
    }

    // --- 席ごとの位置と速度 ---

    // 生存機数は毎フレーム数え直す。撃破で小隊が痩せるため。

    // 各席の実位置と速度は、追従機が「自分の前の機体」を追うのに要る（Formations_Squad.h）。
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

    // --- 小隊ごとの更新 ---

    // 小隊の数だけ回す（ドローンの数ではない）。

    // プレイヤーが居なければ、小隊行動そのものを解除する（Regroup へ落ちる）。
    const bool ordered = hasPlayer && (formation == DroneFormationType::SquadAssault);

    // 並ぶ角度は「生きている小隊の数」で割る。
    // 最大数(kMaxSquads)で割ると、小隊番号は0から詰めて振られるので、数小隊しか居ないとき円の一部に固まる。
    uint8_t activeCount = 0;
    for (const auto &s : reg.squads)
        if (s.aliveCount > 0) ++activeCount;

    uint8_t rank = 0;
    for (uint8_t i = 0; i < SquadRegistry::kMaxSquads; ++i)
    {
        SquadState &s = reg.squads[i];
        if (s.aliveCount == 0) continue;
        UpdateSquad(s, i, rank++, activeCount, ordered, attacking, bossPos, playerPos, playerRunSpeed,
                    reg.tuning, dt);
    }
}

void SquadUpdateSystem::UpdateSquad(SquadState        &squad,
                                    uint8_t            squadId,
                                    uint8_t            rank,
                                    uint8_t            activeCount,
                                    bool               ordered,
                                    bool               attacking,
                                    const Vector3     &bossPos,
                                    const Vector3     &playerPos,
                                    float              playerRunSpeed,
                                    const SquadTuning &tuning,
                                    float              dt)
{
    // 処理フロー:
    // 1. 次の段階を決める
    // 2. 追跡中は、プレイヤーへ曲がりながら飛び続ける
    // 3. 待機・展開中は、目標点へバネで寄る
    // 4. アンカーの向きを決める
    // 5. 床より下へ沈めない（追跡中も同じ）

    // --- 次の段階 ---

    // 優先度順に上から判定し、最初に成立したものを採る。
    SquadPhase next;
    if (ordered == false)
    {
        next = SquadPhase::Regroup; // 別の陣形を指示された＝小隊行動は解除
    }
    else if (attacking)
    {
        // 全小隊が合図と同時に追跡を始め、合図が立っている間ずっと続ける。終わりを決めるのは BT（合図を落とす）。
        // 一斉に襲いかかる物量を見せる技なので、小隊ごとに開始をずらさない。
        next = SquadPhase::Chase;
    }
    else
    {
        next = SquadPhase::Approach;
    }

    const SquadPhase prevPhase = squad.phase;
    squad.phase                = next;

    // --- 追跡 ---

    // アンカーがプレイヤーを追い、後続機はそれに付いていく。
    // 曲がる速さに上限があり、避けられるかどうかはこの上限で決まる。速さは曲がるときだけ落とす。
    if (squad.phase == SquadPhase::Chase)
    {
        // 速さはプレイヤーの走行速度から決め、小隊ごとに少しばらつかせる（理由は SquadTuning::chaseSpeedSpread）。
        // 個体差は小隊番号から決める。生存小隊の中での順番(rank)は撃破で変わり、追跡の途中で速さが跳ねるため。
        const float speedRatio = tuning.chaseSpeedOverRun + tuning.chaseSpeedSpread * SquadVariation(squadId);
        const float topSpeed   = playerRunSpeed * speedRatio;

        // 追跡に入った最初のフレームは、展開中の速さから加速を始める（いきなり最高速にしない）。
        if (prevPhase != SquadPhase::Chase) squad.chaseSpeed = squad.anchorVelocity.Length();

        // 狙いを左右どちらへずらすか。偶数番は左、奇数番は右にして、左右から挟み込む形にする。
        const float aimSide = (squadId % 2 == 0) ? -1.0f : 1.0f;

        const float aimCos = SteerTowardPlayer(squad, playerPos, aimSide, tuning, dt);

        // 狙う方向とのずれが大きいほど遅くする（理由は SquadTuning::turnSlowMinScale）。
        // 正面なら最高速、真横より後ろなら下限の割合。速さの変化は chaseAccel で頭打ちにする。
        const float facing      = std::clamp(aimCos, 0.0f, 1.0f);
        const float targetSpeed = topSpeed * (tuning.turnSlowMinScale + (1.0f - tuning.turnSlowMinScale) * facing);
        const float maxDelta    = tuning.chaseAccel * dt;
        squad.chaseSpeed += std::clamp(targetSpeed - squad.chaseSpeed, -maxDelta, maxDelta);

        squad.anchorVelocity = squad.anchorForward * squad.chaseSpeed;
        squad.anchorPos      = squad.anchorPos + squad.anchorVelocity * dt;
        ClampAltitude(squad, tuning);
        return;
    }

    // --- 待機・展開 ---

    // 目標点をバネで追う。

    // 目標点の角度は小隊ごとにずらす。複数小隊が、別方向から包囲する形に自然になる。

    // 順番を 0〜1 の割合にする。両端に半区間ぶんの余白を取るので、扇でも全周でも等間隔に並び、
    // 全周（360度）のときに最初と最後の小隊が重ならない。
    const float slot01 = (static_cast<float>(rank) + 0.5f) / static_cast<float>(activeCount);

    Vector3 desired;
    if (squad.phase == SquadPhase::Approach)
    {
        // 扇の中心は、プレイヤーから見たボスの方向。
        // カメラはプレイヤーの後ろからボスを向くので、ここを中心にすれば小隊が画面に収まる。
        float center = 0.0f;
        {
            const float dx = bossPos.x - playerPos.x;
            const float dz = bossPos.z - playerPos.z;
            if (dx * dx + dz * dz > 0.001f) center = std::atan2(dz, dx);
        }

        const float arc = DirectX::XMConvertToRadians(tuning.approachArcDeg);
        const float a   = center + (slot01 - 0.5f) * arc +
                        std::sin(m_elapsed * kApproachDrift) * kApproachSway;

        // 隣り合う小隊を、手前・低い段と奥・高い段へ互い違いに振る。
        // 同じ半径と高さに並べると、カメラから見て小隊同士が重なり、数が少なく見える。
        const float layer  = (rank % 2 == 0) ? -1.0f : 1.0f;
        const float radius = tuning.approachRadius + layer * tuning.layerRadiusSpread;
        const float height = tuning.approachHeight + layer * tuning.layerHeightSpread;

        // 斜め横から回り込み、側面から迫られるように見せる。
        desired = playerPos + Vector3(std::cos(a) * radius, height, std::sin(a) * radius);
    }
    else
    {
        // 待機はボスの頭上なので、全周に等間隔で並べて回す。
        const float a = slot01 * kTwoPi + m_elapsed * 0.25f;
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

    // --- 向き ---

    // 展開中はプレイヤーの方を向く（これから来ると読ませる）。待機中は進行方向を向く。
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

    // --- 床 ---

    ClampAltitude(squad, tuning);
}

void SquadUpdateSystem::ClampAltitude(SquadState &squad, const SquadTuning &tuning) const
{
    const float floorY = kSquadGroundY + tuning.minAltitude;
    if (squad.anchorPos.y >= floorY) return;

    squad.anchorPos.y = floorY;

    // 位置を止めるだけでは直らない。
    // 速度が下を向いたままだと、追従機がそれを followFeedForward として受け取って床へ押し込まれ続ける。
    if (squad.anchorVelocity.y < 0.0f) squad.anchorVelocity.y = 0.0f;

    // 向きも水平にする。
    // 真下を向いたまま追跡の速さを掛けると、速度の大半が止められた縦方向に消えて床に張り付く。
    // 水平成分だけ残せば、床の近くを飛んで通り過ぎる。
    if (squad.anchorForward.y < 0.0f)
    {
        squad.anchorForward.y = 0.0f;
        if (squad.anchorForward.LengthSquared() < 0.001f)
            squad.anchorForward = Vector3::UnitZ; // 真下を向いていた場合の退避
        squad.anchorForward.Normalize();
    }
}

float SquadUpdateSystem::SteerTowardPlayer(SquadState        &squad,
                                           const Vector3     &playerPos,
                                           float              aimSide,
                                           const SquadTuning &tuning,
                                           float              dt) const
{
    // 処理フロー:
    // 1. 狙う点を決める（プレイヤーの左右へずらし、近いほどずれを縮める）
    // 2. 狙う点へ、1フレームで曲がれる角度の範囲で向きを寄せる

    const Vector3 toPlayer = playerPos - squad.anchorPos;
    const float   dist     = toPlayer.Length();

    // --- 狙う点 ---

    // 全小隊が同じ点を追うと1か所に集まるので、小隊から見たプレイヤーの左右へずらした点を狙う。
    // ずれは距離に比例して縮め、最後はプレイヤー本人へ向かわせる（ずらしたままだと必ず外れる）。
    Vector3 aimPoint = playerPos;
    {
        // 横方向は水平面で決める。真上から見下ろす位置にいると左右が定まらないので、そのときはずらさない。
        Vector3 side(toPlayer.z, 0.0f, -toPlayer.x);
        const float fadeRange = tuning.aimFadeStartDist - tuning.aimFadeEndDist;
        if (side.LengthSquared() > 0.001f && fadeRange > 0.001f)
        {
            side.Normalize();
            const float fade = std::clamp((dist - tuning.aimFadeEndDist) / fadeRange, 0.0f, 1.0f);
            aimPoint += side * (aimSide * tuning.aimSideOffset * fade);
        }
    }

    // --- 向き ---

    // 終末ロック（一定距離から直線に固定する処理）は持たない。
    // 近づくほどプレイヤーを見る角度は速く変わり、下の旋回上限を超えた時点で曲がりきれなくなる。
    // 至近距離で直線になるのは、この上限から自然に起きる。
    Vector3     toAim   = aimPoint - squad.anchorPos;
    const float aimDist = toAim.Length();
    if (aimDist < 0.001f) return 1.0f;

    toAim /= aimDist;

    // 1フレームで曲がれる角度に上限を掛ける。
    const float maxRad = DirectX::XMConvertToRadians(tuning.maxTurnRateDeg) * dt;
    const float cosA   = std::clamp(squad.anchorForward.Dot(toAim), -1.0f, 1.0f);
    const float angle  = std::acos(cosA);

    if (angle <= 1.0e-4f)
    {
        squad.anchorForward = toAim;
        return cosA;
    }

    const float t       = (std::min)(maxRad / angle, 1.0f);
    Vector3     blended = Vector3::Lerp(squad.anchorForward, toAim, t);
    if (blended.LengthSquared() > 0.001f)
    {
        blended.Normalize();
        squad.anchorForward = blended;
    }
    return cosA;
}

// DroneCommandSystem の後、DroneFormationSystem の前（前後は両者の GetRunAfter で決める）。
REGISTER_LOGIC_SYSTEM(SquadUpdateSystem, Priority::LogicStage::L02_Update)
