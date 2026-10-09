#include "SquadUpdateSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/Components/TimeState.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    // ============================================================
    // 調整値（実行中に触るものは SquadTuning）
    // ============================================================

    // --- 集合 ---

    constexpr float kGatherHighHeight = 30.0f; // 最初に集まる高さ（ボス基準）[m]。地上の間合いを塞がない高さ
    constexpr float kGatherArriveDist = 3.0f;  // 上の集合点に「着いた」とみなす距離 [m]。着いた小隊から降り始める
    constexpr float kDescendSeconds   = 1.0f;  // 上から下まで降りるのにかける時間 [秒]

    // 集合中にアンカーを目標点へ寄せるバネ。追跡より遅くして緩急を作る。
    struct AnchorSpring
    {
        float gain;     // ずれに掛ける強さ [1/s]
        float damping;  // 今の速度を目標の速度へ寄せる速さ [1/s]
        float maxSpeed; // 上限の速さ [m/s]
    };
    constexpr AnchorSpring kAnchorSpring{.gain = 1.8f, .damping = 3.0f, .maxSpeed = 26.0f};

    constexpr float kGatherTurnRate = 4.0f; // 集合中にアンカーをプレイヤーへ向け直す速さ [1/s]

    // --- 追跡 ---

    // 狙う方向とのずれがこの角度までは減速しない [度]。
    // 正面付近の小さな修正舵でまで速さが揺れると、「決まった速さで追ってくる」ように見えない。
    constexpr float kTurnSlowStartDeg = 25.0f;

    // 向きが合ってきたときに旋回を緩める強さ [1/s]。旋回の速さを「残りの角度 × この値」までに抑える。
    // 例えば 3 なら、残り 30 度で 90 度/秒、残り 10 度で 30 度/秒。向きが揃う直前に行き過ぎて左右に振れない。
    constexpr float kTurnEaseGain = 3.0f;

    // ============================================================
    // デバッグ UI の部品
    // ============================================================

    // 直前の項目にマウスを乗せている間だけ説明を出す。
    void HoverTooltip(const char *text)
    {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text);
    }
} // namespace

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void SquadUpdateSystem::Initialize()
{
    if (m_world->HasResource<SquadRegistry>()) return;
    m_world->AddResource<SquadRegistry>();
}

void SquadUpdateSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. このフレームの入力（時間・ボスの命令・プレイヤー）を集める
    // 2. 各小隊の生存数と、席ごとの位置・速度を集め直す
    // 3. 小隊同士の反発を、全小隊を動かす前にまとめて求める
    // 4. 生きている小隊ごとに、段階を決めてアンカーを動かす

    if (!m_world->HasResource<SquadRegistry>()) m_world->AddResource<SquadRegistry>();
    auto &reg = m_world->GetResource<SquadRegistry>();

    // SquadFrame は全小隊に共通なので、Update の先頭で1回だけ集める。
    const SquadFrame frame = ReadFrame(rawDt);

    // --- 席ごとの位置と速度 ---

    // 生存機数は毎フレーム数え直す。撃破で小隊が痩せるため。
    // 各席の実位置と速度は、追従機が「自分の前の機体」を追うのに要る（Formations_Squad.h）。
    // Movement（L03）より前に走るので、ここで読めるのは前フレームの位置になる。
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
    uint8_t activeCount = 0;
    for (const auto &s : reg.squads)
        if (s.aliveCount > 0) ++activeCount;

    ComputeSeparation(reg);

    uint8_t rank = 0;
    for (auto &s : reg.squads)
    {
        if (s.aliveCount == 0) continue;
        UpdateSquad(s, rank++, activeCount, frame, reg.tuning);
    }
}

// ---------------------------------------------------------------------------
// このフレームの入力
// ---------------------------------------------------------------------------

SquadUpdateSystem::SquadFrame SquadUpdateSystem::ReadFrame(float rawDt) const
{
    SquadFrame frame;

    // --- 時間 ---

    // 後続機と同じ時計で進める（DroneFormationSystem と同じ理由）。
    // rawDt のままだと、世界スロー中にアンカーだけ等速で進み、スローで追う後続機との間で隊列がちぎれる。
    frame.dt = rawDt;
    if (m_world->HasResource<TimeContext>())
    {
        const auto &time = m_world->GetResource<TimeContext>();
        frame.dt *= time.globalScale * time.groupScales[static_cast<size_t>(TimeGroup::Enemy)];
    }

    // --- ボス ---

    // 1対1の戦闘なのでボスは1体。最初の1体の命令と位置を読む。
    DroneFormationType formation = DroneFormationType::None;
    auto               bossView  = m_world->View<BossCommandComponent, TransformComponent>();
    for (auto b : bossView)
    {
        if (const auto *cmd = m_world->GetComponent<BossCommandComponent>(b))
        {
            formation       = cmd->requestFormation;
            frame.attacking = cmd->droneAttackActive;
        }
        if (const auto *t = m_world->GetComponent<TransformComponent>(b))
            frame.bossPos = Vector3(t->GetWorldPosition()); // position はローカル
        break;
    }

    // --- プレイヤー ---

    // プレイヤーが居なければ、追跡の目標が無い。小隊行動そのものを解除する（Regroup へ落ちる）。
    bool hasPlayer = false;
    if (m_world->HasResource<BattleContext>())
    {
        const auto &battle = m_world->GetResource<BattleContext>();
        if (battle.HasPlayer())
        {
            frame.playerPos      = Vector3(battle.playerPos);
            frame.playerRunSpeed = battle.playerRunSpeed;
            hasPlayer            = true;
        }
    }

    frame.ordered = hasPlayer && (formation == DroneFormationType::SquadAssault);
    return frame;
}

// ---------------------------------------------------------------------------
// 小隊同士の反発
// ---------------------------------------------------------------------------

void SquadUpdateSystem::ComputeSeparation(SquadRegistry &reg) const
{
    const float radius = reg.tuning.separationRadius;

    for (auto &self : reg.squads)
    {
        self.separation = Vector3::Zero;
        if (self.aliveCount == 0) continue;

        // 最大32個なので総当たりでよい。
        for (const auto &other : reg.squads)
        {
            if (&other == &self || other.aliveCount == 0) continue;

            Vector3     away = self.anchorPos - other.anchorPos;
            const float dist = away.Length();

            // ぴったり重なっていると離れる向きが決まらないので飛ばす（集合の輪で離してあるので起きにくい）。
            if (dist < 0.001f || dist >= radius) continue;

            // 近いほど強く押す。重なっていれば1、半径ちょうどで0。
            away /= dist;
            self.separation += away * (1.0f - dist / radius);
        }
    }
}

// ---------------------------------------------------------------------------
// 段階ごとの動き
// ---------------------------------------------------------------------------

void SquadUpdateSystem::UpdateSquad(SquadState &squad, uint8_t rank, uint8_t activeCount,
                                    const SquadFrame &frame, const SquadTuning &tuning) const
{
    const SquadPhase prevPhase = squad.phase;
    squad.phase                = ChoosePhase(frame);

    // 待機中と、待機から出た最初のフレームは、アンカーを先頭の機に重ねる。
    // 待機を経ずに始まった場合（配備した直後に小隊突撃を指示された）も、原点から飛んでこない。
    if (squad.phase == SquadPhase::Regroup || prevPhase == SquadPhase::Regroup)
        FollowLeadMember(squad);

    switch (squad.phase)
    {
    case SquadPhase::Gather:
        UpdateGather(squad, prevPhase, rank, activeCount, frame, tuning);
        break;
    case SquadPhase::Chase:
        UpdateChase(squad, prevPhase, frame, tuning);
        break;
    default:
        break; // Regroup は上で済んでいる
    }

    ClampAltitude(squad);
}

SquadPhase SquadUpdateSystem::ChoosePhase(const SquadFrame &frame) const
{
    // 別の陣形を指示された＝小隊行動は解除。
    if (!frame.ordered) return SquadPhase::Regroup;

    // 全小隊が合図と同時に追跡を始め、合図が立っている間ずっと続ける。終わりを決めるのは BT（合図を落とす）。
    // 一斉に襲いかかる物量を見せる技なので、小隊ごとに開始をずらさない。
    if (frame.attacking) return SquadPhase::Chase;

    // 指示はあるが合図はまだ＝予備動作。
    return SquadPhase::Gather;
}

void SquadUpdateSystem::FollowLeadMember(SquadState &squad) const
{
    for (size_t slot = 0; slot < squad.memberValid.size(); ++slot)
    {
        if (!squad.memberValid[slot]) continue;

        squad.anchorPos      = squad.memberPos[slot];
        squad.anchorVelocity = squad.memberVel[slot];

        Vector3 dir = squad.memberVel[slot];
        if (dir.LengthSquared() > 0.001f)
        {
            dir.Normalize();
            squad.anchorForward = dir;
        }
        return;
    }
}

void SquadUpdateSystem::UpdateGather(SquadState &squad, SquadPhase prevPhase, uint8_t rank, uint8_t activeCount,
                                     const SquadFrame &frame, const SquadTuning &tuning) const
{
    // --- 目標点 ---

    // 集合に入った最初のフレームは、上から始める。
    if (prevPhase != SquadPhase::Gather) squad.descend = 0.0f;

    // 順番を 0〜1 の割合にして、輪を等分する。
    // 最大数（kMaxSquads）ではなく生きている小隊の数で割る。数小隊しか居ないとき、輪の一部に固まらないように。
    const float slot01 = (static_cast<float>(rank) + 0.5f) / static_cast<float>(activeCount);
    const float angle  = slot01 * DirectX::XM_2PI;

    const Vector3 ring(std::cos(angle) * tuning.gatherRadius, 0.0f, std::sin(angle) * tuning.gatherRadius);
    const Vector3 high = frame.bossPos + ring + Vector3::UnitY * kGatherHighHeight;
    const Vector3 low  = frame.bossPos + ring + Vector3::UnitY * tuning.gatherLowHeight;

    // 上の集合点に着いた小隊から降り始める。一度降り始めたら、止めずに最後まで降りる。
    if (squad.descend > 0.0f || Vector3::Distance(squad.anchorPos, high) < kGatherArriveDist)
        squad.descend = (std::min)(1.0f, squad.descend + frame.dt / kDescendSeconds);

    const Vector3 desired = Vector3::Lerp(high, low, squad.descend);

    // --- 移動 ---

    // 目標点へバネで寄せる。瞬間移動させず、上限の速さで飛んでいく。
    Vector3 targetVel = (desired - squad.anchorPos) * kAnchorSpring.gain;
    if (targetVel.LengthSquared() > kAnchorSpring.maxSpeed * kAnchorSpring.maxSpeed)
    {
        targetVel.Normalize();
        targetVel *= kAnchorSpring.maxSpeed;
    }
    squad.anchorVelocity = Vector3::Lerp(squad.anchorVelocity, targetVel,
                                         (std::min)(kAnchorSpring.damping * frame.dt, 1.0f));
    squad.anchorPos      = squad.anchorPos + squad.anchorVelocity * frame.dt;

    // --- 向き ---

    // プレイヤーの方を向く（これから来ると読ませる）。
    Vector3 face(frame.playerPos.x - squad.anchorPos.x, 0.0f, frame.playerPos.z - squad.anchorPos.z);
    if (face.LengthSquared() > 0.001f)
    {
        face.Normalize();
        Vector3 blended = Vector3::Lerp(squad.anchorForward, face, (std::min)(kGatherTurnRate * frame.dt, 1.0f));
        if (blended.LengthSquared() > 0.001f)
        {
            blended.Normalize();
            squad.anchorForward = blended;
        }
    }
}

float SquadUpdateSystem::SteerToward(SquadState &squad, const Vector3 &target, const SquadTuning &tuning,
                                     float dt) const
{
    Vector3     toTarget = target - squad.anchorPos;
    const float dist     = toTarget.Length();
    if (dist < 0.001f) return 1.0f;
    toTarget /= dist;

    const float cosA  = std::clamp(squad.anchorForward.Dot(toTarget), -1.0f, 1.0f);
    const float angle = std::acos(cosA);

    // --- 旋回の速さ ---

    // 欲しい速さは、上限と「残りの角度 × kTurnEaseGain」の小さい方。
    // 上げるときは turnAccelDeg で少しずつ（曲がり始めがゆっくり）、下げるときはすぐ（行き過ぎて振れないように）。
    const float wantRate = (std::min)(tuning.maxTurnRateDeg, DirectX::XMConvertToDegrees(angle) * kTurnEaseGain);
    if (wantRate > squad.turnRateDeg)
        squad.turnRateDeg = (std::min)(wantRate, squad.turnRateDeg + tuning.turnAccelDeg * dt);
    else
        squad.turnRateDeg = wantRate;

    if (angle <= 1.0e-4f)
    {
        squad.anchorForward = toTarget;
        return cosA;
    }

    // --- 向き ---

    // 今の向きと狙う方向を含む面の中で、1フレームぶんの角度だけ回す。
    // Lerp で寄せると角度が正確に出ず、真後ろを狙ったときは横へ回らずに向きが一瞬で反転する。
    Vector3 axis = squad.anchorForward.Cross(toTarget);
    if (axis.LengthSquared() < 1.0e-6f) axis = Vector3::UnitY; // 真後ろ：水平に回って向き直る
    axis.Normalize();

    const float step    = (std::min)(DirectX::XMConvertToRadians(squad.turnRateDeg) * dt, angle);
    Vector3     rotated = Vector3::Transform(squad.anchorForward, Quaternion::CreateFromAxisAngle(axis, step));
    if (rotated.LengthSquared() > 0.001f)
    {
        rotated.Normalize();
        squad.anchorForward = rotated;
    }
    return cosA;
}

void SquadUpdateSystem::UpdateChase(SquadState &squad, SquadPhase prevPhase, const SquadFrame &frame,
                                    const SquadTuning &tuning) const
{
    // --- 狙い ---

    // プレイヤーへの向きに、他の小隊から離れる向きを混ぜる。
    // プレイヤーに近いほど反発を弱め、最後は必ずプレイヤーを狙う（止まっている相手に当てるため）。
    Vector3     aim          = frame.playerPos;
    Vector3     toPlayer     = frame.playerPos - squad.anchorPos;
    const float distToPlayer = toPlayer.Length();
    if (distToPlayer > 0.001f && tuning.separationFadeDist > 0.001f)
    {
        toPlayer /= distToPlayer;
        const float fade = std::clamp(distToPlayer / tuning.separationFadeDist, 0.0f, 1.0f);
        const Vector3 aimDir = toPlayer + squad.separation * (tuning.separationWeight * fade);

        // SteerToward は位置を受け取るので、混ぜた向きの先に点を置く（向きだけが効く）。
        aim = squad.anchorPos + aimDir * distToPlayer;
    }

    // --- 向き ---

    // 追跡に入った最初のフレームは、今の速さから加速を始め（いきなり最高速にしない）、旋回も止まった状態から始める。
    if (prevPhase != SquadPhase::Chase)
    {
        squad.chaseSpeed  = squad.anchorVelocity.Length();
        squad.turnRateDeg = 0.0f;
    }

    const float aimCos = SteerToward(squad, aim, tuning, frame.dt);

    // --- 速さ ---

    // 狙う方向とのずれが kTurnSlowStartDeg までは最高速のまま。そこから 90 度にかけて下限の割合まで落とす
    // （減速する理由は turnSlowMinScale の説明）。
    const float topSpeed    = frame.playerRunSpeed * tuning.chaseSpeedOverRun;
    const float slowStart   = std::cos(DirectX::XMConvertToRadians(kTurnSlowStartDeg));
    const float facing      = std::clamp(aimCos / slowStart, 0.0f, 1.0f);
    const float targetSpeed = topSpeed * (tuning.turnSlowMinScale + (1.0f - tuning.turnSlowMinScale) * facing);

    // 減速と再加速で変化の速さを分ける。どちらも緩やかにして、速さが瞬時に切り替わらないようにする。
    const float rate     = (targetSpeed < squad.chaseSpeed) ? tuning.chaseBrake : tuning.chaseAccel;
    const float maxDelta = rate * frame.dt;
    squad.chaseSpeed += std::clamp(targetSpeed - squad.chaseSpeed, -maxDelta, maxDelta);

    // --- 移動 ---

    squad.anchorVelocity = squad.anchorForward * squad.chaseSpeed;
    squad.anchorPos      = squad.anchorPos + squad.anchorVelocity * frame.dt;
}

void SquadUpdateSystem::ClampAltitude(SquadState &squad) const
{
    if (squad.anchorPos.y >= kSquadFloorY) return;

    squad.anchorPos.y = kSquadFloorY;

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

// ---------------------------------------------------------------------------
// 小隊への割り当て
// ---------------------------------------------------------------------------

void SquadUpdateSystem::AssignNewMembers(World &world)
{
    // 1小隊は8機以下である前提でビットマスクを使う（空きスロット探索用）。
    static_assert(SquadRegistry::kSquadSize <= 8, "小隊の人数が uint8_t のビット数を超えている");

    // 使用中のスロットを小隊ごとに集める。
    uint8_t usedSlots[SquadRegistry::kMaxSquads] = {};
    {
        auto scan = world.View<DroneComponent>();
        for (auto e : scan)
        {
            const auto *d = world.GetComponent<DroneComponent>(e);
            if (!d) continue;
            if (d->squadId >= SquadRegistry::kMaxSquads) continue;
            if (d->slotInSquad >= SquadRegistry::kSquadSize) continue;
            usedSlots[d->squadId] =
                static_cast<uint8_t>(usedSlots[d->squadId] | (1u << d->slotInSquad));
        }
    }

    // 未所属の機だけを小隊へ入れる。
    auto view = world.View<DroneComponent>();
    for (auto e : view)
    {
        auto *d = world.GetComponent<DroneComponent>(e);
        if (!d) continue;
        if (d->squadId < SquadRegistry::kMaxSquads) continue;

        // 空いている席から先に埋めるので、同じスロットに2機が重なることはない。
        bool placed = false;
        for (uint8_t s = 0; s < SquadRegistry::kMaxSquads && !placed; ++s)
        {
            for (uint8_t slot = 0; slot < SquadRegistry::kSquadSize; ++slot)
            {
                if (usedSlots[s] & (1u << slot)) continue;

                d->squadId     = s;
                d->slotInSquad = slot;
                usedSlots[s]   = static_cast<uint8_t>(usedSlots[s] | (1u << slot));
                placed         = true;
                break;
            }
        }
        // 満杯（32小隊×5機＝160機を超えた分）なら無所属のまま。その機は大技だけで使われる。
    }
}

// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void SquadUpdateSystem::OnGui()
{
    if (!ImGui::CollapsingHeader("Squad (小隊の調整と一覧)")) return;
    if (!m_world->HasResource<SquadRegistry>()) return;
    auto &reg = m_world->GetResource<SquadRegistry>();

    // --- 調整値 ---

    // しなり具合と避けやすさは目で見ないと決まらないので、実行中に変えられるようにする。
    auto &tuning = reg.tuning;
    if (ImGui::TreeNodeEx("小隊の調整", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "隊列");
        ImGui::SliderFloat("機体の間隔", &tuning.chainGap, 1.0f, 10.0f, "%.1f m");
        HoverTooltip("前の機体との距離。小さいほど隊列が詰まって1本に見える");
        ImGui::SliderFloat("追従バネ", &tuning.followGain, 1.0f, 30.0f, "%.1f");
        HoverTooltip("間隔のずれを直す強さ。小さいほど隊列が伸び縮みして柔らかく見える");

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.5f, 1.0f), "追跡");
        ImGui::SliderFloat("速さ（走行比）", &tuning.chaseSpeedOverRun, 0.5f, 2.0f, "x%.2f");
        HoverTooltip("プレイヤーの走行速度に掛ける倍率。1 を超えると走って逃げても追いつかれる");
        ImGui::SliderFloat("旋回上限", &tuning.maxTurnRateDeg, 10.0f, 360.0f, "%.0f deg/s");
        HoverTooltip("回避はこの制限だけで成り立つ。上げすぎると切り返しても曲がって当たる");
        ImGui::SliderFloat("旋回の立ち上がり", &tuning.turnAccelDeg, 10.0f, 1000.0f, "%.0f deg/s2");
        HoverTooltip("曲がり始めに旋回を速めていく速さ。小さいほど切り返しへの反応が遅れ、ゆっくり曲がり始める");
        ImGui::SliderFloat("曲がるときの速さ", &tuning.turnSlowMinScale, 0.1f, 1.0f, "x%.2f");
        HoverTooltip("大きく曲がるとき（90度以上）に落とす速さの下限。1 で減速しない");
        ImGui::SliderFloat("減速", &tuning.chaseBrake, 1.0f, 60.0f, "%.1f m/s2");
        HoverTooltip("曲がるときに速さを落とす速さ。小さいほど勢いで流れてから曲がる");
        ImGui::SliderFloat("再加速", &tuning.chaseAccel, 1.0f, 60.0f, "%.1f m/s2");
        HoverTooltip("向き直った後に速さを戻す速さ。追跡を始めたときの加速も兼ねる");

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "小隊同士の反発");
        ImGui::SliderFloat("反発の半径", &tuning.separationRadius, 2.0f, 30.0f, "%.1f m");
        HoverTooltip("他の小隊のアンカーがこの距離より近いと離れようとする。小隊1本の長さ（間隔×4）より長くする");
        ImGui::SliderFloat("反発の強さ", &tuning.separationWeight, 0.0f, 3.0f, "%.2f");
        HoverTooltip("狙う方向に混ぜる割合。0 で反発なし（全小隊が1本に重なる元の動き）");
        ImGui::SliderFloat("反発を弱める距離", &tuning.separationFadeDist, 1.0f, 40.0f, "%.0f m");
        HoverTooltip("プレイヤーからこの距離より内側では反発を弱め、プレイヤーの位置で0にする。小さいほど直前まで広がるが当たりにくい");

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "集合");
        ImGui::SliderFloat("輪の半径", &tuning.gatherRadius, 5.0f, 60.0f, "%.0f m");
        HoverTooltip("ボスの周りに集まる輪の大きさ");
        ImGui::SliderFloat("降りた高さ", &tuning.gatherLowHeight, 0.0f, 20.0f, "%.0f m");
        HoverTooltip("ボスの足元からの高さ。降り切ったこの高さから追跡が始まる");

        if (ImGui::Button("既定値に戻す", ImVec2(140, 24)))
            tuning = SquadTuning{};

        ImGui::TreePop();
    }

    // --- 一覧 ---

    // 生きている小隊のアンカーの位置・段階・反発の大きさを、先頭から6個まで出す。
    int shown = 0;
    for (uint8_t i = 0; i < SquadRegistry::kMaxSquads && shown < 6; ++i)
    {
        const auto &s = reg.squads[i];
        if (s.aliveCount == 0) continue;

        ImGui::Text("小隊%2d: %d機  %-8s  (%.0f, %.0f, %.0f)  反発 %.2f",
                    i, s.aliveCount, SquadPhaseName(s.phase),
                    s.anchorPos.x, s.anchorPos.y, s.anchorPos.z, s.separation.Length());
        ++shown;
    }
}

const char *SquadUpdateSystem::SquadPhaseName(SquadPhase phase) const
{
    switch (phase)
    {
    case SquadPhase::Regroup: return "Regroup";
    case SquadPhase::Gather: return "Gather";
    default: return "Chase";
    }
}

// DroneCommandSystem の後、DroneFormationSystem の前（前後は両者の GetRunAfter で決める）。
REGISTER_LOGIC_SYSTEM(SquadUpdateSystem, Priority::LogicStage::L02_Update)
