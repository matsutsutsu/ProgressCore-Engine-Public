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

    // --- 待機 ---

    constexpr float kRegroupHeight = 22.0f; // ボス頭上のどの高さで待つか [m]。地上の間合いを塞がない高さ
    constexpr float kRegroupRadius = 18.0f; // ボス中心からの水平距離 [m]。小隊ごとに角度をずらして並ぶ
    constexpr float kRegroupSpin   = 0.25f; // 待機の輪を回す速さ [rad/s]

    // --- 集合 ---

    constexpr float kGatherHighHeight = 30.0f; // 最初に集まる高さ（ボス基準）[m]。地上の間合いを塞がない高さ
    constexpr float kGatherArriveDist = 3.0f;  // 上の集合点に「着いた」とみなす距離 [m]。着いた小隊から降り始める
    constexpr float kDescendSeconds   = 1.0f;  // 上から下まで降りるのにかける時間 [秒]

    // --- 待機・集合で共通 ---

    // アンカーを目標点へ寄せるバネ。追跡より遅くして緩急を作る。
    struct AnchorSpring
    {
        float gain;     // ずれに掛ける強さ [1/s]
        float damping;  // 今の速度を目標の速度へ寄せる速さ [1/s]
        float maxSpeed; // 上限の速さ [m/s]
    };
    constexpr AnchorSpring kAnchorSpring{.gain = 1.8f, .damping = 3.0f, .maxSpeed = 26.0f};

    constexpr float kAnchorTurnRate = 4.0f; // アンカーの向きを寄せる速さ [1/s]

    // --- 一覧 ---

    constexpr int kListMaxRows = 6; // デバッグ UI の一覧に出す小隊の数

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
    // 3. 生きている小隊ごとに、段階を決めてアンカーを動かす

    if (!m_world->HasResource<SquadRegistry>()) m_world->AddResource<SquadRegistry>();
    auto &reg = m_world->GetResource<SquadRegistry>();

    // SquadFrame は全小隊に共通なので、Update の先頭で1回だけ集める。
    const SquadFrame frame = ReadFrame(rawDt);
    m_elapsed += frame.dt;

    GatherMembers(reg);

    // 小隊の数だけ回す（ドローンの数ではない）。
    uint8_t activeCount = 0;
    for (const auto &s : reg.squads)
        if (s.aliveCount > 0) ++activeCount;

    uint8_t rank = 0;
    for (uint8_t i = 0; i < SquadRegistry::kMaxSquads; ++i)
    {
        SquadState &s = reg.squads[i];
        if (s.aliveCount == 0) continue;
        UpdateSquad(s, i, rank++, activeCount, frame, reg.tuning);
    }
}

// ---------------------------------------------------------------------------
// 入力の収集
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

void SquadUpdateSystem::GatherMembers(SquadRegistry &reg)
{
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
}

// ---------------------------------------------------------------------------
// 段階ごとの動き
// ---------------------------------------------------------------------------

void SquadUpdateSystem::UpdateSquad(SquadState &squad, uint8_t squadId, uint8_t rank, uint8_t activeCount,
                                    const SquadFrame &frame, const SquadTuning &tuning) const
{
    const SquadPhase prevPhase = squad.phase;
    squad.phase                = ChoosePhase(frame);

    switch (squad.phase)
    {
    case SquadPhase::Regroup:
        UpdateRegroup(squad, rank, activeCount, frame);
        break;
    case SquadPhase::Gather:
        UpdateGather(squad, prevPhase, rank, activeCount, frame, tuning);
        break;
    case SquadPhase::Chase:
        UpdateChase(squad, squadId, prevPhase, frame, tuning);
        break;
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

void SquadUpdateSystem::UpdateRegroup(SquadState &squad, uint8_t rank, uint8_t activeCount,
                                      const SquadFrame &frame) const
{
    // 全周に等間隔で並べて回す。
    // 順番を 0〜1 の割合にし、最大数（kMaxSquads）ではなく生きている小隊の数で割る。数小隊しか居ないとき、輪の一部に固まらないように。
    const float   slot01  = (static_cast<float>(rank) + 0.5f) / static_cast<float>(activeCount);
    const float   angle   = slot01 * DirectX::XM_2PI + m_elapsed * kRegroupSpin;
    const Vector3 desired = frame.bossPos + Vector3(std::cos(angle) * kRegroupRadius,
                                                    kRegroupHeight,
                                                    std::sin(angle) * kRegroupRadius);

    SpringAnchorTo(squad, desired, frame.dt);

    // 進行方向を向く。
    TurnAnchorToward(squad, squad.anchorVelocity, frame.dt);
}

void SquadUpdateSystem::UpdateGather(SquadState &squad, SquadPhase prevPhase, uint8_t rank, uint8_t activeCount,
                                     const SquadFrame &frame, const SquadTuning &tuning) const
{
    // --- 目標点 ---

    // 集合に入った最初のフレームは、上から始める。
    if (prevPhase != SquadPhase::Gather) squad.descend = 0.0f;

    // 順番を 0〜1 の割合にして、輪を等分する（割る数の理由は UpdateRegroup と同じ）。
    const float slot01 = (static_cast<float>(rank) + 0.5f) / static_cast<float>(activeCount);
    const float angle  = slot01 * DirectX::XM_2PI;

    const Vector3 ring(std::cos(angle) * tuning.gatherRadius, 0.0f, std::sin(angle) * tuning.gatherRadius);
    const Vector3 high = frame.bossPos + ring + Vector3::UnitY * kGatherHighHeight;
    const Vector3 low  = frame.bossPos + ring + Vector3::UnitY * tuning.gatherLowHeight;

    // 上の集合点に着いた小隊から降り始める。一度降り始めたら、止めずに最後まで降りる。
    if (squad.descend > 0.0f || Vector3::Distance(squad.anchorPos, high) < kGatherArriveDist)
        squad.descend = (std::min)(1.0f, squad.descend + frame.dt / kDescendSeconds);

    SpringAnchorTo(squad, Vector3::Lerp(high, low, squad.descend), frame.dt);

    // --- 向き ---

    // プレイヤーの方を向く（これから来ると読ませる）。
    TurnAnchorToward(squad, Vector3(frame.playerPos.x - squad.anchorPos.x, 0.0f, frame.playerPos.z - squad.anchorPos.z),
                     frame.dt);
}

void SquadUpdateSystem::UpdateChase(SquadState &squad, uint8_t squadId, SquadPhase prevPhase,
                                    const SquadFrame &frame, const SquadTuning &tuning) const
{
    // --- 向き ---

    // 狙いを左右どちらへずらすか。偶数番は左、奇数番は右にして、左右から挟み込む形にする。
    const float aimSide = (squadId % 2 == 0) ? -1.0f : 1.0f;
    const float aimCos  = SteerTowardPlayer(squad, frame.playerPos, aimSide, tuning, frame.dt);

    // --- 速さ ---

    // 速さはプレイヤーの走行速度から決め、小隊ごとに少しばらつかせる（理由は SquadTuning::chaseSpeedSpread）。
    // 個体差は小隊番号から決める。生存小隊の中での順番（rank）は撃破で変わり、追跡の途中で速さが跳ねるため。
    const float speedRatio = tuning.chaseSpeedOverRun + tuning.chaseSpeedSpread * SquadVariation(squadId);
    const float topSpeed   = frame.playerRunSpeed * speedRatio;

    // 追跡に入った最初のフレームは、集合中の速さから加速を始める（いきなり最高速にしない）。
    if (prevPhase != SquadPhase::Chase) squad.chaseSpeed = squad.anchorVelocity.Length();

    // 狙う方向とのずれが大きいほど遅くする（理由は SquadTuning::turnSlowMinScale）。
    // 正面なら最高速、真横より後ろなら下限の割合。速さの変化は chaseAccel で頭打ちにする。
    const float facing      = std::clamp(aimCos, 0.0f, 1.0f);
    const float targetSpeed = topSpeed * (tuning.turnSlowMinScale + (1.0f - tuning.turnSlowMinScale) * facing);
    const float maxDelta    = tuning.chaseAccel * frame.dt;
    squad.chaseSpeed += std::clamp(targetSpeed - squad.chaseSpeed, -maxDelta, maxDelta);

    // --- 移動 ---

    squad.anchorVelocity = squad.anchorForward * squad.chaseSpeed;
    squad.anchorPos      = squad.anchorPos + squad.anchorVelocity * frame.dt;
}

float SquadUpdateSystem::SteerTowardPlayer(SquadState &squad, const Vector3 &playerPos, float aimSide,
                                           const SquadTuning &tuning, float dt) const
{
    // --- 狙う点 ---

    // 全小隊が同じ点を追うと1か所に集まるので、小隊から見たプレイヤーの左右へずらした点を狙う。
    // ずれは距離に比例して縮め、最後はプレイヤー本人へ向かわせる（ずらしたままだと必ず外れる）。
    const Vector3 toPlayer = playerPos - squad.anchorPos;
    const float   dist     = toPlayer.Length();

    Vector3 aimPoint = playerPos;
    {
        // 横方向は水平面で決める。真上から見下ろす位置にいると左右が定まらないので、そのときはずらさない。
        Vector3     side(toPlayer.z, 0.0f, -toPlayer.x);
        const float fadeRange = tuning.aimFadeStartDist - tuning.aimFadeEndDist;
        if (side.LengthSquared() > 0.001f && fadeRange > 0.001f)
        {
            side.Normalize();
            const float fade = std::clamp((dist - tuning.aimFadeEndDist) / fadeRange, 0.0f, 1.0f);
            aimPoint += side * (aimSide * tuning.aimSideOffset * fade);
        }
    }

    // --- 向き ---

    Vector3     toAim   = aimPoint - squad.anchorPos;
    const float aimDist = toAim.Length();
    if (aimDist < 0.001f) return 1.0f;
    toAim /= aimDist;

    const float cosA  = std::clamp(squad.anchorForward.Dot(toAim), -1.0f, 1.0f);
    const float angle = std::acos(cosA);
    if (angle <= 1.0e-4f)
    {
        squad.anchorForward = toAim;
        return cosA;
    }

    // 1フレームで曲がれる角度までしか寄せない。
    const float maxRad  = DirectX::XMConvertToRadians(tuning.maxTurnRateDeg) * dt;
    const float t       = (std::min)(maxRad / angle, 1.0f);
    Vector3     blended = Vector3::Lerp(squad.anchorForward, toAim, t);
    if (blended.LengthSquared() > 0.001f)
    {
        blended.Normalize();
        squad.anchorForward = blended;
    }
    return cosA;
}

void SquadUpdateSystem::SpringAnchorTo(SquadState &squad, const Vector3 &desired, float dt) const
{
    // 初回だけ目標へ瞬間移動させる（ワールド原点から飛んでくるのを防ぐ）。
    if (!squad.initialized)
    {
        squad.initialized    = true;
        squad.anchorPos      = desired;
        squad.anchorVelocity = Vector3::Zero;
    }

    Vector3 targetVel = (desired - squad.anchorPos) * kAnchorSpring.gain;
    if (targetVel.LengthSquared() > kAnchorSpring.maxSpeed * kAnchorSpring.maxSpeed)
    {
        targetVel.Normalize();
        targetVel *= kAnchorSpring.maxSpeed;
    }
    squad.anchorVelocity = Vector3::Lerp(squad.anchorVelocity, targetVel,
                                         (std::min)(kAnchorSpring.damping * dt, 1.0f));
    squad.anchorPos      = squad.anchorPos + squad.anchorVelocity * dt;
}

void SquadUpdateSystem::TurnAnchorToward(SquadState &squad, Vector3 face, float dt) const
{
    if (face.LengthSquared() <= 0.001f) return;
    face.Normalize();

    Vector3 blended = Vector3::Lerp(squad.anchorForward, face, (std::min)(kAnchorTurnRate * dt, 1.0f));
    if (blended.LengthSquared() > 0.001f)
    {
        blended.Normalize();
        squad.anchorForward = blended;
    }
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

float SquadUpdateSystem::SquadVariation(uint8_t squadId) const
{
    const float x = static_cast<float>(squadId) * 0.6180340f;
    return (x - std::floor(x)) * 2.0f - 1.0f;
}

// ---------------------------------------------------------------------------
// 小隊への割り当て
// ---------------------------------------------------------------------------

void SquadUpdateSystem::AssignNewMembers(World &world)
{
    // 1小隊は8機以下である前提でビットマスクを使う（空きスロット探索用）。
    static_assert(SquadRegistry::kSquadSize <= 8, "小隊の人数が uint8_t のビット数を超えている");

    // --- 使用中の席 ---

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

    // --- 未所属の機を入れる ---

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

    DrawTuning(reg.tuning);
    DrawSquadList(reg);
}

void SquadUpdateSystem::DrawTuning(SquadTuning &tuning) const
{
    if (!ImGui::TreeNodeEx("小隊の調整", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "隊列");
    ImGui::SliderFloat("機体の間隔", &tuning.chainGap, 1.0f, 10.0f, "%.1f m");
    HoverTooltip("前の機体との距離。小さいほど隊列が詰まって1本に見える");
    ImGui::SliderFloat("追従バネ", &tuning.followGain, 1.0f, 30.0f, "%.1f");
    HoverTooltip("間隔のずれを直す強さ。小さいほど隊列が伸び縮みして柔らかく見える");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.5f, 1.0f), "追跡");
    ImGui::SliderFloat("速さ（走行比）", &tuning.chaseSpeedOverRun, 0.5f, 2.0f, "x%.2f");
    HoverTooltip("プレイヤーの走行速度に掛ける倍率。1 を超えると走って逃げても追いつかれる");
    ImGui::SliderFloat("速さのばらつき", &tuning.chaseSpeedSpread, 0.0f, 0.5f, "±%.2f");
    HoverTooltip("小隊ごとに上の倍率へ足す幅。一番遅い小隊でも 1 を超えるようにする");
    ImGui::SliderFloat("狙いの横ずれ", &tuning.aimSideOffset, 0.0f, 30.0f, "%.1f m");
    HoverTooltip("狙う点をプレイヤーの左右へずらす量。小隊ごとに左右互い違いになる");
    ImGui::SliderFloat("ずれを縮め始める距離", &tuning.aimFadeStartDist, 1.0f, 80.0f, "%.0f m");
    ImGui::SliderFloat("ずれが0になる距離", &tuning.aimFadeEndDist, 0.0f, 40.0f, "%.0f m");
    HoverTooltip("この距離より内側ではプレイヤー本人を狙う");
    ImGui::SliderFloat("旋回時の最低速度", &tuning.turnSlowMinScale, 0.1f, 1.0f, "x%.2f");
    HoverTooltip("大きく曲がるとき（90度以上）に落とす速さの下限");
    ImGui::SliderFloat("加減速", &tuning.chaseAccel, 5.0f, 300.0f, "%.0f m/s2");
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

void SquadUpdateSystem::DrawSquadList(const SquadRegistry &reg) const
{
    int shown = 0;
    for (uint8_t i = 0; i < SquadRegistry::kMaxSquads && shown < kListMaxRows; ++i)
    {
        const auto &s = reg.squads[i];
        if (s.aliveCount == 0) continue;

        ImGui::Text("小隊%2d: %d機  %-8s  (%.0f, %.0f, %.0f)",
                    i, s.aliveCount, SquadPhaseName(s.phase),
                    s.anchorPos.x, s.anchorPos.y, s.anchorPos.z);
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
