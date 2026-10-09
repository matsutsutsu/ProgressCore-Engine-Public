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

    // 曲がるときに落とす速さの下限（追跡の速さに対する割合）。狙う方向とのずれが 90 度以上でこの割合になる。
    //
    // 一定の速さのままだと、一度通り過ぎた相手が旋回の円（半径＝速さ÷旋回上限）の内側に入り、
    // どれだけ曲がっても機首を向けられずに周りを回り続ける。止まっている相手に当たらなくなる。
    // 減速すれば旋回半径が縮み、小さく U ターンして向き直れる。
    constexpr float kTurnSlowMinScale = 0.4f;

    // 速さを変えられる上限 [m/s²]（加速・減速とも）。
    // 無制限だと、至近距離で切り返された瞬間にも急ブレーキして小回りで追ってくるので、
    // 旋回上限で避けるという前提が崩れる。ブレーキが間に合うのは、通り過ぎた後の U ターンだけにする。
    constexpr float kChaseAccel = 60.0f;

    // ============================================================
    // このフレームの入力
    // ============================================================

    // 全小隊に共通の、このフレームの入力。Update の先頭で1回だけ集める。
    struct SquadFrame
    {
        bool    ordered        = false; // 小隊突撃が指示されていて、プレイヤーも居る
        bool    attacking      = false; // 攻撃の合図が立っている
        Vector3 bossPos        = Vector3::Zero;
        Vector3 playerPos      = Vector3::Zero;
        float   playerRunSpeed = 0.0f; // 追跡の速さの基準 [m/s]
        float   dt             = 0.0f; // 敵の時間で進めた経過時間 [秒]
    };

    SquadFrame ReadFrame(World &world, float rawDt)
    {
        SquadFrame frame;

        // --- 時間 ---

        // 後続機と同じ時計で進める（DroneFormationSystem と同じ理由）。
        // rawDt のままだと、世界スロー中にアンカーだけ等速で進み、スローで追う後続機との間で隊列がちぎれる。
        frame.dt = rawDt;
        if (world.HasResource<TimeContext>())
        {
            const auto &time = world.GetResource<TimeContext>();
            frame.dt *= time.globalScale * time.groupScales[static_cast<size_t>(TimeGroup::Enemy)];
        }

        // --- ボス ---

        // 1対1の戦闘なのでボスは1体。最初の1体の命令と位置を読む。
        DroneFormationType formation = DroneFormationType::None;
        auto               bossView  = world.View<BossCommandComponent, TransformComponent>();
        for (auto b : bossView)
        {
            if (const auto *cmd = world.GetComponent<BossCommandComponent>(b))
            {
                formation       = cmd->requestFormation;
                frame.attacking = cmd->droneAttackActive;
            }
            if (const auto *t = world.GetComponent<TransformComponent>(b))
                frame.bossPos = Vector3(t->GetWorldPosition()); // position はローカル
            break;
        }

        // --- プレイヤー ---

        // プレイヤーが居なければ、追跡の目標が無い。小隊行動そのものを解除する（Regroup へ落ちる）。
        bool hasPlayer = false;
        if (world.HasResource<BattleContext>())
        {
            const auto &battle = world.GetResource<BattleContext>();
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

    // ============================================================
    // 段階ごとの動き
    // ============================================================

    // 次の段階を決める。優先度順に上から判定し、最初に成立したものを採る。
    SquadPhase ChoosePhase(const SquadFrame &frame)
    {
        // 別の陣形を指示された＝小隊行動は解除。
        if (!frame.ordered) return SquadPhase::Regroup;

        // 全小隊が合図と同時に追跡を始め、合図が立っている間ずっと続ける。終わりを決めるのは BT（合図を落とす）。
        // 一斉に襲いかかる物量を見せる技なので、小隊ごとに開始をずらさない。
        if (frame.attacking) return SquadPhase::Chase;

        // 指示はあるが合図はまだ＝予備動作。
        return SquadPhase::Gather;
    }

    /**
     * @brief 待機（Regroup）: アンカーを先頭の機に重ねておく。
     *
     * この間、機体は別の陣形で動いていて、アンカーは誰も追っていない。
     * 先頭の機に重ねておけば、小隊突撃が始まった瞬間にアンカーが今いる場所から滑らかに動き出す。
     *
     * @param squad アンカーを置く小隊。生きている一番前の席の機に重ねる。
     */
    void FollowLeadMember(SquadState &squad)
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

    /**
     * @brief 集合（Gather）: ボスの頭上の輪の自分の場所へ集まり、着いたら下へ降りてくる。
     *
     * 小隊ごとに輪の上の角度をずらすので、ボスを囲むように並ぶ。
     * 集まって降りてくる動きそのものが予告で、降り切った場所から追跡が始まる。
     *
     * @param squad       動かす小隊。
     * @param prevPhase   前フレームの段階。集合に入った最初のフレームを見分ける。
     * @param rank        生きている小隊の中での順番（0〜activeCount-1）。輪の上の位置を決める。
     * @param activeCount 生きている小隊の数。
     * @param frame       このフレームの入力。
     * @param tuning      小隊の調整値。
     */
    void UpdateGather(SquadState &squad, SquadPhase prevPhase, uint8_t rank, uint8_t activeCount,
                      const SquadFrame &frame, const SquadTuning &tuning)
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

    /**
     * @brief アンカーの向きを target の方へ、旋回上限の範囲で曲げる。
     *
     * 終末ロック（一定距離から直線に固定する処理）は持たない。
     * 近づくほどプレイヤーを見る角度は速く変わり、旋回上限を超えた時点で曲がりきれなくなる。
     * 至近距離で直線になるのは、この上限から自然に起きる。
     *
     * @param squad          向きを曲げる小隊。
     * @param target         向かう先（ワールド）。
     * @param maxTurnRateDeg 曲がれる速さの上限 [度/秒]。
     * @param dt             経過時間 [秒]。
     * @return 曲げる前の向きと target の方向のなす角の cos（1=正面、0=真横）。曲がるときの減速に使う。
     */
    float SteerToward(SquadState &squad, const Vector3 &target, float maxTurnRateDeg, float dt)
    {
        Vector3     toTarget = target - squad.anchorPos;
        const float dist     = toTarget.Length();
        if (dist < 0.001f) return 1.0f;
        toTarget /= dist;

        const float cosA  = std::clamp(squad.anchorForward.Dot(toTarget), -1.0f, 1.0f);
        const float angle = std::acos(cosA);
        if (angle <= 1.0e-4f)
        {
            squad.anchorForward = toTarget;
            return cosA;
        }

        // 1フレームで曲がれる角度までしか寄せない。
        const float maxRad  = DirectX::XMConvertToRadians(maxTurnRateDeg) * dt;
        const float t       = (std::min)(maxRad / angle, 1.0f);
        Vector3     blended = Vector3::Lerp(squad.anchorForward, toTarget, t);
        if (blended.LengthSquared() > 0.001f)
        {
            blended.Normalize();
            squad.anchorForward = blended;
        }
        return cosA;
    }

    /**
     * @brief 追跡（Chase）: アンカーがプレイヤーを追い続ける。後続機はそれに付いていく。
     *
     * 曲がる速さに上限があり、避けられるかどうかはこの上限で決まる。速さは曲がるときだけ落とす。
     *
     * @param squad     動かす小隊。
     * @param prevPhase 前フレームの段階。追跡に入った最初のフレームを見分ける。
     * @param frame     このフレームの入力。
     * @param tuning    小隊の調整値。
     */
    void UpdateChase(SquadState &squad, SquadPhase prevPhase, const SquadFrame &frame, const SquadTuning &tuning)
    {
        // --- 向き ---

        const float aimCos = SteerToward(squad, frame.playerPos, tuning.maxTurnRateDeg, frame.dt);

        // --- 速さ ---

        // 追跡に入った最初のフレームは、今の速さから加速を始める（いきなり最高速にしない）。
        if (prevPhase != SquadPhase::Chase) squad.chaseSpeed = squad.anchorVelocity.Length();

        // 狙う方向とのずれが大きいほど遅くする（理由は kTurnSlowMinScale）。
        // 正面なら最高速、真横より後ろなら下限の割合。速さの変化は kChaseAccel で頭打ちにする。
        const float topSpeed    = frame.playerRunSpeed * tuning.chaseSpeedOverRun;
        const float facing      = std::clamp(aimCos, 0.0f, 1.0f);
        const float targetSpeed = topSpeed * (kTurnSlowMinScale + (1.0f - kTurnSlowMinScale) * facing);
        const float maxDelta    = kChaseAccel * frame.dt;
        squad.chaseSpeed += std::clamp(targetSpeed - squad.chaseSpeed, -maxDelta, maxDelta);

        // --- 移動 ---

        squad.anchorVelocity = squad.anchorForward * squad.chaseSpeed;
        squad.anchorPos      = squad.anchorPos + squad.anchorVelocity * frame.dt;
    }

    /**
     * @brief アンカーが床へ沈まないように高度を止める。全段階の最後に必ず通す。
     *
     * 位置だけでなく、速度と向きの下向き成分も抜く。
     * 追従機は「1つ前の機体の速度」を先渡しで受け取るので、アンカーが下を向いたままだと後続機が床へ押し込まれ続ける。
     *
     * @param squad 高度を止める小隊。
     */
    void ClampAltitude(SquadState &squad)
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

    /**
     * @brief 小隊1個ぶんの段階を決め、アンカーを動かす。
     * @param squad       更新する小隊。
     * @param rank        生きている小隊の中での順番（0〜activeCount-1）。
     * @param activeCount 生きている小隊の数。
     * @param frame       このフレームの入力。
     * @param tuning      小隊の調整値。
     */
    void UpdateSquad(SquadState &squad, uint8_t rank, uint8_t activeCount,
                     const SquadFrame &frame, const SquadTuning &tuning)
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

    // ============================================================
    // デバッグ UI の部品
    // ============================================================

    // 直前の項目にマウスを乗せている間だけ説明を出す。
    void HoverTooltip(const char *text)
    {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text);
    }

    const char *SquadPhaseName(SquadPhase phase)
    {
        switch (phase)
        {
        case SquadPhase::Regroup: return "Regroup";
        case SquadPhase::Gather: return "Gather";
        default: return "Chase";
        }
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
    // 3. 生きている小隊ごとに、段階を決めてアンカーを動かす

    if (!m_world->HasResource<SquadRegistry>()) m_world->AddResource<SquadRegistry>();
    auto &reg = m_world->GetResource<SquadRegistry>();

    // SquadFrame は全小隊に共通なので、Update の先頭で1回だけ集める。
    const SquadFrame frame = ReadFrame(*m_world, rawDt);

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

    uint8_t rank = 0;
    for (auto &s : reg.squads)
    {
        if (s.aliveCount == 0) continue;
        UpdateSquad(s, rank++, activeCount, frame, reg.tuning);
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
        ImGui::SliderFloat("旋回上限", &tuning.maxTurnRateDeg, 10.0f, 720.0f, "%.0f deg/s");
        HoverTooltip("回避はこの制限だけで成り立つ。上げすぎると切り返しても曲がって当たる");

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

    // 生きている小隊のアンカーの位置と段階を、先頭から6個まで出す。
    int shown = 0;
    for (uint8_t i = 0; i < SquadRegistry::kMaxSquads && shown < 6; ++i)
    {
        const auto &s = reg.squads[i];
        if (s.aliveCount == 0) continue;

        ImGui::Text("小隊%2d: %d機  %-8s  (%.0f, %.0f, %.0f)",
                    i, s.aliveCount, SquadPhaseName(s.phase),
                    s.anchorPos.x, s.anchorPos.y, s.anchorPos.z);
        ++shown;
    }
}

// DroneCommandSystem の後、DroneFormationSystem の前（前後は両者の GetRunAfter で決める）。
REGISTER_LOGIC_SYSTEM(SquadUpdateSystem, Priority::LogicStage::L02_Update)
