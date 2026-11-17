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

    // --- 降下 ---

    constexpr float kAnchorTurnRate = 4.0f; // 降下中にアンカーをプレイヤーへ向け直す速さ [1/s]

    // --- 一覧 ---

    constexpr int kListMaxRows = 6; // デバッグ UI の一覧に出す小隊の数

    // ============================================================
    // デバッグ UI の部品
    // ============================================================

    // 直前の項目にマウスを乗せている間だけ説明を出す
    void HoverTooltip(const char *text)
    {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text);
    }
}

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

    const SquadFrame frame = ReadFrame(rawDt);

    GatherMembers(reg);

    // 小隊の数だけ回す（ドローンの数ではない）
    for (uint8_t i = 0; i < SquadRegistry::kMaxSquads; ++i)
    {
        SquadState &s = reg.squads[i];
        if (s.aliveCount == 0) continue;
        UpdateSquad(s, i, frame, reg.tuning);
    }
}



// ---------------------------------------------------------------------------
// 入力の収集
// ---------------------------------------------------------------------------

SquadUpdateSystem::SquadFrame SquadUpdateSystem::ReadFrame(float rawDt) const
{
    SquadFrame frame;

    // --- 時間 ---

    // 後続機と同じ時計で進める。rawDt だとスロー中にアンカーだけ等速で進み、隊列がちぎれる
    frame.dt = rawDt;
    if (m_world->HasResource<TimeContext>())
    {
        const auto &time = m_world->GetResource<TimeContext>();
        frame.dt *= time.globalScale * time.groupScales[static_cast<size_t>(TimeGroup::Enemy)];
    }

    // --- ボス ---

    // 1対1の戦闘なので、最初の1体の命令を読む
    DroneFormationType formation = DroneFormationType::None;
    auto               bossView  = m_world->View<BossCommandComponent>();
    for (auto b : bossView)
    {
        if (const auto *cmd = m_world->GetComponent<BossCommandComponent>(b))
        {
            formation       = cmd->requestFormation;
            frame.attacking = cmd->droneAttackActive;
        }
        break;
    }

    // --- プレイヤー ---

    // プレイヤーがいなければ追う相手がないので、小隊行動を解除する（Idle へ落ちる）
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
// アンカーの動き
// ---------------------------------------------------------------------------

void SquadUpdateSystem::UpdateSquad(SquadState &squad, uint8_t squadId, const SquadFrame &frame,
                                    const SquadTuning &tuning) const
{
    const SquadPhase prevPhase = squad.phase;
    squad.phase                = ChoosePhase(frame);

    switch (squad.phase)
    {
    case SquadPhase::Idle:
        FollowLeadMember(squad);
        return; // 機体の位置に重ねるだけなので、高度は止めない
    case SquadPhase::Descend:
        UpdateDescend(squad, prevPhase, frame, tuning);
        break;
    case SquadPhase::Chase:
        UpdateChase(squad, squadId, prevPhase, frame, tuning);
        break;
    }

    ClampAltitude(squad);
}



SquadPhase SquadUpdateSystem::ChoosePhase(const SquadFrame &frame) const
{
    if (!frame.ordered) return SquadPhase::Idle;

    // 一斉に襲いかかる物量を見せる技なので、全小隊が合図と同時に追跡を始める（終わりは BT が合図を落とす）
    if (frame.attacking) return SquadPhase::Chase;

    // 指示はあるが合図はまだ＝予備動作
    return SquadPhase::Descend;
}



Vector3 SquadUpdateSystem::FollowLeadMember(SquadState &squad) const
{
    for (size_t slot = 0; slot < squad.memberValid.size(); ++slot)
    {
        if (!squad.memberValid[slot]) continue;

        squad.anchorPos      = squad.memberPos[slot];
        squad.anchorVelocity = Vector3::Zero;

        // 向きは先頭の機の進行方向に合わせる。追跡の曲がり始めがここから自然につながる
        const Vector3 leadVel = squad.memberVel[slot];
        if (leadVel.LengthSquared() > 0.001f)
        {
            Vector3 dir = leadVel;
            dir.Normalize();
            squad.anchorForward = dir;
        }
        return leadVel;
    }
    return Vector3::Zero;
}



void SquadUpdateSystem::UpdateDescend(SquadState &squad, SquadPhase prevPhase, const SquadFrame &frame,
                                      const SquadTuning &tuning) const
{
    // --- 降り始め ---

    // 降下に入った瞬間のアンカーの位置から降りる
    // 直前まで先頭の機に重ねてあるので、頭上円陣の輪の上（ボスが滞空していればその高さ）から始まる
    if (prevPhase != SquadPhase::Descend)
    {
        squad.descendFrom = squad.anchorPos;
        squad.descend     = 0.0f;
    }

    // --- 位置 ---

    // 真下の、地面から決まった高さへ降りる。ボスの高さは使わない（滞空中のボスを基準にすると下まで降りない）
    // 始まりが既にそれより低ければ、その高さのまま留まる（上がらない）
    const float   bottomY = (std::min)(squad.descendFrom.y, kDroneGroundY + tuning.descendHeight);
    const Vector3 bottom(squad.descendFrom.x, bottomY, squad.descendFrom.z);

    if (tuning.descendTime > 0.0f)
    {
        squad.descend = (std::min)(1.0f, squad.descend + frame.dt / tuning.descendTime);
    }
    else
    {
        squad.descend = 1.0f;
    }

    // ゆっくり動き出し、ゆっくり止まる
    const float ease = squad.descend * squad.descend * (3.0f - 2.0f * squad.descend);

    const Vector3 prevPos = squad.anchorPos;
    squad.anchorPos       = Vector3::Lerp(squad.descendFrom, bottom, ease);

    // 速度は位置の変化から出す。後続機へ先渡しされ、隊列が上へ尾を引いて降りてくる
    squad.anchorVelocity = (frame.dt > 0.0f) ? (squad.anchorPos - prevPos) / frame.dt : Vector3::Zero;

    // --- 向き ---

    // プレイヤーの方を向き、これから来ると読ませる
    TurnAnchorToward(squad, Vector3(frame.playerPos.x - squad.anchorPos.x, 0.0f, frame.playerPos.z - squad.anchorPos.z),
                     frame.dt);
}



void SquadUpdateSystem::UpdateChase(SquadState &squad, uint8_t squadId, SquadPhase prevPhase,
                                    const SquadFrame &frame, const SquadTuning &tuning) const
{
    // 追跡に入った最初のフレームの速さ。降下から来たら降りてきた勢いから、
    // 予備動作なしで始まったら先頭の機の今の速さから始める（0 から加速すると先頭の機が一瞬止まって見える）
    if (prevPhase == SquadPhase::Descend)
    {
        squad.chaseSpeed = squad.anchorVelocity.Length();
    }
    else if (prevPhase == SquadPhase::Idle)
    {
        squad.chaseSpeed = FollowLeadMember(squad).Length();
    }

    // --- 向き ---

    // 偶数番は左、奇数番は右へ狙いをずらし、左右から挟み込む
    const float aimSide = (squadId % 2 == 0) ? -1.0f : 1.0f;
    const float aimCos  = SteerTowardPlayer(squad, frame.playerPos, aimSide, tuning, frame.dt);

    // --- 速さ ---

    // 走り続けても追いつかれ、ダッシュなら振り切れる倍率にする（1 を超え、ダッシュ÷走行 未満）
    // 倍率で持つので、プレイヤーの速度を変えても関係が崩れない

    // 小隊ごとに速さをばらすと旋回半径も着く時刻も変わり、全小隊が同じ線に重ならない
    // 一番遅い小隊でも倍率が 1 を超える幅にする（下回ると走って逃げるプレイヤーに追いつけない）

    // 個体差は小隊番号から決める。生存小隊の中での順番は撃破で変わり、追跡の途中で速さが跳ねるため
    const float speedRatio = tuning.chaseSpeedOverRun + tuning.chaseSpeedSpread * SquadVariation(squadId);
    const float topSpeed   = frame.playerRunSpeed * speedRatio;

    // 狙う方向とのずれが大きいほど遅くする（正面なら最高速、真横より後ろなら下限の割合）
    // 一定の速さだと、通り過ぎた相手が旋回の円の内側に入り、周りを回り続けて当たらない。減速すれば小さく U ターンできる

    // 速さの変化は chaseAccel で頭打ちにする
    // 無制限だと至近距離で切り返されても急ブレーキで小回りし、旋回上限で避けるという前提が崩れる
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

    // 全小隊が同じ点を追うと1か所に集まるので、プレイヤーの左右へずらした点を狙う
    // ずれは近づくほど縮め、最後はプレイヤー本人へ向かわせる（ずらしたままだと最後まで横をかすめて外れる）
    const Vector3 toPlayer = playerPos - squad.anchorPos;
    const float   dist     = toPlayer.Length();

    Vector3 aimPoint = playerPos;
    {
        // 横方向は水平面で決める。真上にいると左右が定まらないので、そのときはずらさない
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

    // 1フレームで曲がれる角度までしか寄せない。回避はこの上限だけで成り立つ
    // （上げすぎると切り返しても曲がって当たり、ジャスト回避が成立しない）
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

    // 速度が下を向いたままだと、追従機がそれを先渡しで受け取って床へ押し込まれ続ける
    if (squad.anchorVelocity.y < 0.0f) squad.anchorVelocity.y = 0.0f;

    // 向きも水平にする。真下を向いたままだと、追跡の速さが止められた縦方向に消えて床に張り付く
    if (squad.anchorForward.y < 0.0f)
    {
        squad.anchorForward.y = 0.0f;
        if (squad.anchorForward.LengthSquared() < 0.001f)
            squad.anchorForward = Vector3::UnitZ; // 真下を向いていた場合
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
    // 空き席をビットマスクで探すので、1小隊は8機以下
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
        // 満杯なら無所属のまま。その機は大技だけで使われる
    }
}



// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void SquadUpdateSystem::OnGui()
{
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
    ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "降下（予備動作）");
    ImGui::SliderFloat("降りた高さ", &tuning.descendHeight, 0.0f, 20.0f, "%.0f m");
    HoverTooltip("地面からの高さ。降り切ったこの高さから追跡が始まる");
    ImGui::SliderFloat("降りる時間", &tuning.descendTime, 0.2f, 5.0f, "%.1f s");
    HoverTooltip("頭上円陣から降り切るまでの秒数。BT の予備動作の Wait をこれ以上にする");

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

        ImGui::Text("小隊%2d: %d機  %-6s  (%.0f, %.0f, %.0f)",
                    i, s.aliveCount, SquadPhaseName(s.phase),
                    s.anchorPos.x, s.anchorPos.y, s.anchorPos.z);
        ++shown;
    }
}



const char *SquadUpdateSystem::SquadPhaseName(SquadPhase phase) const
{
    switch (phase)
    {
    case SquadPhase::Idle: return "Idle";
    case SquadPhase::Descend: return "Descend";
    default: return "Chase";
    }
}



// DroneCommandSystem の後、DroneFormationSystem の前
REGISTER_LOGIC_SYSTEM(SquadUpdateSystem, Priority::LogicStage::L02_Update)
