#include "SquadUpdateSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
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
    constexpr float kAnchorTurnRate = 4.0f; // 扇へ並ぶ間に、アンカーをプレイヤーへ向け直す速さ [1/s]

    constexpr float kArcEnd = 2.0f; // 道筋の進み具合の終わり（1本目 0〜1 と2本目 1〜2 を通しで数える）

    constexpr float kCoastTimeLimit = 1.0f; // 道筋の先へ直進してよい時間の上限 [秒]。最後の機が終点を越えられない形のときの保険



    // 2次ベジェ曲線の t の位置。p0 から出て、p1 の方へ引っぱられながら p2 に着く
    Vector3 BezierPoint(const Vector3 &p0, const Vector3 &p1, const Vector3 &p2, float t)
    {
        const float s = 1.0f - t;
        return p0 * (s * s) + p1 * (2.0f * s * t) + p2 * (t * t);
    }



    // 2次ベジェ曲線の t の接線（t で微分した値）。向きが進行方向で、長さが「t を1進めたときの移動量」
    Vector3 BezierTangent(const Vector3 &p0, const Vector3 &p1, const Vector3 &p2, float t)
    {
        return (p1 - p0) * (2.0f * (1.0f - t)) + (p2 - p1) * (2.0f * t);
    }



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
    // 1. 席ごとの位置・速度を集め直す
    // 2. このフレームの入力（時間・ボスの命令・ボスとプレイヤーの位置）を集める
    // 3. 小隊ごとに段階を決めて、アンカーを動かす

    if (!m_world->HasResource<SquadRegistry>()) m_world->AddResource<SquadRegistry>();
    auto &reg = m_world->GetResource<SquadRegistry>();

    const int        squadCount = GatherMembers(reg);
    const SquadFrame frame      = ReadFrame(rawDt, squadCount, reg.tuning);

    // 小隊の数だけ回す（ドローンの数ではない）。機体のいない小隊のアンカーは、誰も読まない
    for (uint8_t i = 0; i < SquadRegistry::kMaxSquads; ++i)
        UpdateSquad(reg.squads[i], i, frame, reg.tuning);
}



// ---------------------------------------------------------------------------
// 入力の収集
// ---------------------------------------------------------------------------

int SquadUpdateSystem::GatherMembers(SquadRegistry &reg)
{
    int squadCount = 0;

    ForEach([&](const DroneComponent &drone, const TransformComponent &trans) {
        if (drone.squadId >= SquadRegistry::kMaxSquads) return; // 無所属（大技用）
        if (drone.slotInSquad >= SquadRegistry::kSquadSize) return;

        SquadState &squad = reg.squads[drone.squadId];
        squad.memberPos[drone.slotInSquad] = Vector3(trans.position);
        squad.memberVel[drone.slotInSquad] = drone.currentVelocity;

        // 小隊は番号の若い方から埋まるので、一番大きい番号＋1 がいる小隊の数になる
        squadCount = (std::max)(squadCount, drone.squadId + 1);
    });

    return squadCount;
}



SquadUpdateSystem::SquadFrame SquadUpdateSystem::ReadFrame(float rawDt, int squadCount, const SquadTuning &tuning) const
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

    // 1対1の戦闘なので、最初の1体の命令と位置を読む
    DroneFormationType formation = DroneFormationType::None;
    bool               hasBoss   = false;

    auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
    for (auto b : bossView)
    {
        if (const auto *cmd = m_world->GetComponent<BossCommandComponent>(b))
        {
            formation    = cmd->requestFormation;
            frame.attack = cmd->droneAttackActive;
        }
        if (const auto *t = m_world->GetComponent<TransformComponent>(b))
        {
            frame.bossPos = t->GetWorldPosition(); // position はローカル。扇はワールドで置く
            hasBoss       = true;
        }
        break;
    }

    // --- プレイヤー ---

    bool hasPlayer = false;
    if (m_world->HasResource<BattleContext>())
    {
        const auto &battle = m_world->GetResource<BattleContext>();
        if (battle.HasPlayer())
        {
            frame.playerPos = Vector3(battle.playerCenter);
            hasPlayer       = true;
        }
    }

    // --- 扇の向き ---

    // 扇はボスからプレイヤーへの水平の向きに開く
    Vector3 aim = frame.playerPos - frame.bossPos;
    aim.y       = 0.0f;
    if (aim.LengthSquared() < 0.001f) aim = Vector3::UnitZ;
    aim.Normalize();
    frame.aimDir = aim;

    // 飛ばす数は、いる小隊の数を超えない。扇の間隔を、実際に並ぶ数で割るため
    frame.launchCount = std::clamp(tuning.launchCount, 0, squadCount);

    // ボスかプレイヤーがいなければ、扇の要も狙う相手もないので小隊行動をしない
    frame.ordered = hasBoss && hasPlayer && (formation == DroneFormationType::SquadArc);
    return frame;
}



// ---------------------------------------------------------------------------
// アンカーの動き
// ---------------------------------------------------------------------------

void SquadUpdateSystem::UpdateSquad(SquadState &squad, uint8_t squadId, const SquadFrame &frame,
                                    const SquadTuning &tuning) const
{
    const SquadPhase prevPhase = squad.phase;
    squad.phase                = ChoosePhase(squad, squadId, frame);

    switch (squad.phase)
    {
    case SquadPhase::Idle:
    case SquadPhase::Return:
        FollowLeadMember(squad);
        break;
    case SquadPhase::FormFan:
        UpdateFormFan(squad, squadId, prevPhase, frame, tuning);
        break;
    case SquadPhase::Arc:
        UpdateArc(squad, squadId, prevPhase, frame, tuning);
        break;
    }
}



SquadPhase SquadUpdateSystem::ChoosePhase(const SquadState &squad, uint8_t squadId, const SquadFrame &frame) const
{
    const SquadPhase prevPhase = squad.phase;

    // 番号の若い小隊から launchCount 個を飛ばす。残りは頭上円陣で待つ
    if (!frame.ordered || squadId >= frame.launchCount) return SquadPhase::Idle;

    // 撃つ合図の前は扇で構える
    if (!frame.attack) return SquadPhase::FormFan;

    // 円陣から直接は飛ばさず、1フレームでも扇へ並び始めてから飛ぶ（予告の時間が 0 の技でも、席を離れる向きが揃う）
    if (prevPhase == SquadPhase::Idle) return SquadPhase::FormFan;

    // 飛び終わったら戻る。合図が残っていても Return に留め、扇へ並び直してもう一度飛ばない
    if (prevPhase == SquadPhase::Return) return SquadPhase::Return;

    // 飛び終わりは、アンカーでなく本物の機で決める。アンカーは機体より先を行くので、アンカーで決めると後ろの機が届く前に戻る
    // 最後の機が終点を越えられない形（通り過ぎる距離が短く真上へ抜けるなど）でも、直進が一定時間を過ぎたら戻す
    if (prevPhase == SquadPhase::Arc && squad.arcProgress >= kArcEnd &&
        (HasTailPassed(squad) || squad.coastTime >= kCoastTimeLimit))
        return SquadPhase::Return;

    // 扇に並び終わる前に合図が来たら、並ぶ途中の位置から飛ぶ
    return SquadPhase::Arc;
}



void SquadUpdateSystem::FollowLeadMember(SquadState &squad) const
{
    squad.anchorPos      = squad.memberPos[0];
    squad.anchorVelocity = Vector3::Zero;

    // 向きは先頭の機の進行方向に合わせる。動き出しがここから自然につながる
    const Vector3 leadVel = squad.memberVel[0];
    if (leadVel.LengthSquared() > 0.001f)
    {
        Vector3 dir = leadVel;
        dir.Normalize();
        squad.anchorForward = dir;
    }
}



void SquadUpdateSystem::UpdateFormFan(SquadState &squad, uint8_t squadId, SquadPhase prevPhase,
                                      const SquadFrame &frame, const SquadTuning &tuning) const
{
    // --- 並び始め ---

    // 入った瞬間のアンカーの位置から動く。直前まで先頭の機に重ねてあるので、頭上円陣の席から始まる
    if (prevPhase != SquadPhase::FormFan)
    {
        squad.formFrom     = squad.anchorPos;
        squad.formProgress = 0.0f;
    }

    // --- 位置 ---

    // 扇の位置はボスとプレイヤーに合わせて毎フレーム出し直す。並び終わった後もボスに付いていく
    const Vector3 fanPos = FanPosition(squadId, frame, tuning);

    if (tuning.fanFormTime > 0.0f)
    {
        squad.formProgress = (std::min)(1.0f, squad.formProgress + frame.dt / tuning.fanFormTime);
    }
    else
    {
        squad.formProgress = 1.0f;
    }

    // ゆっくり動き出し、ゆっくり止まる
    const float ease = squad.formProgress * squad.formProgress * (3.0f - 2.0f * squad.formProgress);

    const Vector3 prevPos = squad.anchorPos;
    squad.anchorPos       = Vector3::Lerp(squad.formFrom, fanPos, ease);

    // 速度は位置の変化から出す。後続機へ先渡しされ、隊列が尾を引いて付いてくる
    squad.anchorVelocity = (frame.dt > 0.0f) ? (squad.anchorPos - prevPos) / frame.dt : Vector3::Zero;

    // --- 向き ---

    // プレイヤーへ向ける。並び終わると隊列がプレイヤーを指し、これから飛んでくると読ませる
    TurnAnchorToward(squad, frame.playerPos - squad.anchorPos, frame.dt);
}



Vector3 SquadUpdateSystem::FanPosition(uint8_t squadId, const SquadFrame &frame, const SquadTuning &tuning) const
{
    // 扇の要はボスの真上。小隊を要のまわりの弧の上へ、端から等間隔に並べる
    // 弧は「上」と「横」の面にあり、真上を中心に左右へ開く（プレイヤーから見て正面に広がる）
    const float angle = FanAngle(squadId, frame, tuning);

    const Vector3 rightDir(frame.aimDir.z, 0.0f, -frame.aimDir.x); // 扇の向きに対する横
    const Vector3 pivotPos = frame.bossPos + Vector3::UnitY * tuning.fanPivotHeight;

    return pivotPos + (rightDir * std::sin(angle) + Vector3::UnitY * std::cos(angle)) * tuning.fanRadius;
}



float SquadUpdateSystem::FanAngle(uint8_t squadId, const SquadFrame &frame, const SquadTuning &tuning) const
{
    // 1小隊だけなら真上
    if (frame.launchCount <= 1) return 0.0f;

    // 左端（0）から右端（1）までを等間隔に割り、真ん中を 0 にずらしてから開きの角度を掛ける
    const float ratio = static_cast<float>(squadId) / static_cast<float>(frame.launchCount - 1);
    return (ratio - 0.5f) * DirectX::XMConvertToRadians(tuning.fanSpreadDeg);
}



void SquadUpdateSystem::UpdateArc(SquadState &squad, uint8_t squadId, SquadPhase prevPhase,
                                  const SquadFrame &frame, const SquadTuning &tuning) const
{
    // 処理フロー:
    // 1. 入った瞬間に道筋を決める
    // 2. 道筋の上を一定の速さで進める。道筋の終わりから先は、最後の向きのまま直進する
    // 3. アンカーの速度を書く

    // --- 発射 ---

    if (prevPhase != SquadPhase::Arc) BeginArc(squad, squadId, frame, tuning);

    const Vector3 prevPos = squad.anchorPos;

    if (squad.arcProgress < kArcEnd)
    {
        // --- 道筋の上 ---

        // 進み具合を一定の割合で進めると、曲線の点の間隔がばらばらなので速さが変わってしまう
        // そこで「今の点で進み具合を1進めると何 m 動くか」（接線の長さ）で割り、毎フレーム同じ距離だけ進める
        Vector3 tangent;
        SampleArc(squad, squad.arcProgress, tangent);

        const float metersPerProgress = tangent.Length();
        if (metersPerProgress > 0.001f) squad.arcProgress += tuning.arcSpeed * frame.dt / metersPerProgress;
        squad.arcProgress = (std::min)(squad.arcProgress, kArcEnd);

        squad.anchorPos = SampleArc(squad, squad.arcProgress, tangent);

        // 向きは道筋の進行方向
        if (tangent.LengthSquared() > 0.001f)
        {
            tangent.Normalize();
            squad.anchorForward = tangent;
        }
    }
    else
    {
        // --- 道筋の先 ---

        // アンカーは実体がなく、機体より先を行く。道筋の終わりで止めると、まだ後ろにいる機まで手前で止まってしまう
        // そこで最後の機が終点を越えるまで、同じ速さで直進して隊列を引っぱり続ける
        squad.anchorPos += squad.anchorForward * (tuning.arcSpeed * frame.dt);
        squad.coastTime += frame.dt;
    }

    // --- 速度 ---

    // 速度は位置の変化から出す。後続機へ先渡しされ、隊列がアンカーの道筋をなぞる
    squad.anchorVelocity = (frame.dt > 0.0f) ? (squad.anchorPos - prevPos) / frame.dt : Vector3::Zero;
}



bool SquadUpdateSystem::HasTailPassed(const SquadState &squad) const
{
    // 最後の席の機が、終点を通る「入ってきた向きに垂直な面」を越えたか。越えていれば小隊の全機が終点を抜けている
    const Vector3 tailPos = squad.memberPos[SquadRegistry::kSquadSize - 1];
    return (tailPos - squad.arcTo).Dot(squad.arriveDir) > 0.0f;
}



void SquadUpdateSystem::BeginArc(SquadState &squad, uint8_t squadId, const SquadFrame &frame,
                                 const SquadTuning &tuning) const
{
    // 処理フロー:
    // 1. 始点と終点を決める
    // 2. 1本目の曲がり角を決める
    // 3. 通り過ぎる2本目を決める

    // --- 始点と終点 ---

    // 始点は今のアンカー（扇の位置。並び終わる前に撃たれたら並ぶ途中の位置）
    squad.arcFrom = squad.anchorPos;

    // 終点は撃った瞬間のプレイヤー。この後プレイヤーが動いても追わないので、撃たれてから動けば避けられる
    // 床より下には置かない（後続機の持ち場も同じ高さで止めている）
    squad.arcTo   = frame.playerPos;
    squad.arcTo.y = (std::max)(squad.arcTo.y, kSquadFloorY);

    // --- 1本目の曲がり角 ---

    // 始点と終点の水平の中間を、終点と同じ高さに置く
    // 曲線は曲がり角の方へ引っぱられるので、降下しながら曲がり、終点には水平に突っ込む
    const Vector3 midPos = (squad.arcFrom + squad.arcTo) * 0.5f;
    squad.arcCorner      = Vector3(midPos.x, squad.arcTo.y, midPos.z);

    // 扇の端の小隊ほど、外側へふくらませる。左右の小隊が交差するように来て、横へ1歩動くだけでは全部は避けられない
    const Vector3 rightDir(frame.aimDir.z, 0.0f, -frame.aimDir.x);
    squad.arcCorner += rightDir * (std::sin(FanAngle(squadId, frame, tuning)) * tuning.arcSideBulge);

    // --- 2本目（通り過ぎる） ---

    // 終点に入ってきた向きのまま抜ける。2本目の曲がり角をその向きの先に置くと、つなぎ目で折れない
    Vector3 arriveDir = squad.arcTo - squad.arcCorner;
    arriveDir.y       = 0.0f;
    if (arriveDir.LengthSquared() < 0.001f) arriveDir = frame.aimDir; // 曲がり角が終点に重なったら、ボスからプレイヤーの向きで代用
    arriveDir.Normalize();

    squad.passCorner = squad.arcTo + arriveDir * (tuning.passDistance * 0.5f);
    squad.passTo     = squad.arcTo + arriveDir * tuning.passDistance + Vector3::UnitY * tuning.passClimb;
    squad.arriveDir  = arriveDir;

    squad.arcProgress = 0.0f;
    squad.coastTime   = 0.0f;
}



Vector3 SquadUpdateSystem::SampleArc(const SquadState &squad, float progress, Vector3 &outTangent) const
{
    // 1本目
    if (progress < 1.0f)
    {
        outTangent = BezierTangent(squad.arcFrom, squad.arcCorner, squad.arcTo, progress);
        return BezierPoint(squad.arcFrom, squad.arcCorner, squad.arcTo, progress);
    }

    // 2本目。進み具合の 1〜2 を、曲線の 0〜1 に直して使う
    const float t = (std::min)(progress - 1.0f, 1.0f);
    outTangent    = BezierTangent(squad.arcTo, squad.passCorner, squad.passTo, t);
    return BezierPoint(squad.arcTo, squad.passCorner, squad.passTo, t);
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



// ---------------------------------------------------------------------------
// 小隊への割り当て
// ---------------------------------------------------------------------------

void SquadUpdateSystem::AssignSeat(DroneComponent &drone)
{
    // 通し番号の順に5機ずつ区切る。ドローンは撃破されないので、席は先頭から欠けずに埋まる
    // 小隊の数を超えた機は無所属のまま。その機は大技だけで使われる
    const int squad = drone.localIndex / SquadRegistry::kSquadSize;
    if (squad >= SquadRegistry::kMaxSquads)
    {
        drone.squadId     = SquadRegistry::kNoSquad;
        drone.slotInSquad = 0;
        return;
    }

    drone.squadId     = static_cast<uint8_t>(squad);
    drone.slotInSquad = static_cast<uint8_t>(drone.localIndex % SquadRegistry::kSquadSize);
}



// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void SquadUpdateSystem::OnGui()
{
    if (!m_world->HasResource<SquadRegistry>()) return;
    DrawTuning(m_world->GetResource<SquadRegistry>().tuning);
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
    ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "扇（予備動作）");
    ImGui::SliderInt("飛ばす小隊の数", &tuning.launchCount, 1, SquadRegistry::kMaxSquads);
    HoverTooltip("1回の技で一気に飛ばす小隊の数。いる小隊の数より多くしても、いる数までしか出ない");
    ImGui::SliderFloat("扇の半径", &tuning.fanRadius, 2.0f, 40.0f, "%.1f m");
    HoverTooltip("扇の要から小隊までの距離");
    ImGui::SliderFloat("扇の開き", &tuning.fanSpreadDeg, 0.0f, 180.0f, "%.0f deg");
    HoverTooltip("左端の小隊から右端の小隊までの角度。180 で真横まで開く");
    ImGui::SliderFloat("扇の要の高さ", &tuning.fanPivotHeight, 0.0f, 30.0f, "%.1f m");
    HoverTooltip("ボスの位置から扇の要までの高さ");
    ImGui::SliderFloat("並ぶ時間", &tuning.fanFormTime, 0.1f, 5.0f, "%.1f s");
    HoverTooltip("頭上円陣から扇の位置へ並ぶまでの秒数");
    ImGui::SliderFloat("構える時間", &tuning.fanHoldTime, 0.0f, 5.0f, "%.1f s");
    HoverTooltip("並び終わってから撃つまでの秒数。プレイヤーが狙いを読む時間");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.5f, 1.0f), "弧（攻撃）");
    ImGui::SliderFloat("飛ぶ速さ", &tuning.arcSpeed, 10.0f, 560.0f, "%.0f m/s");
    HoverTooltip("アンカーが道筋を進む速さ。速いほど出だしで隊列が伸び、しなりが大きくなる");
    ImGui::SliderFloat("横のふくらみ", &tuning.arcSideBulge, 0.0f, 30.0f, "%.1f m");
    HoverTooltip("扇の端の小隊ほど外側へふくらんで飛ぶ。0 で全小隊がまっすぐプレイヤーへ向かう");
    ImGui::SliderFloat("通り過ぎる距離", &tuning.passDistance, 0.0f, 40.0f, "%.1f m");
    HoverTooltip("撃った瞬間のプレイヤー位置を抜けてから、水平に飛ぶ距離");
    ImGui::SliderFloat("抜けた後の上昇", &tuning.passClimb, 0.0f, 20.0f, "%.1f m");
    HoverTooltip("通り過ぎる間に上がる高さ");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "技の時間割");
    ImGui::SliderFloat("撃ってから戻すまで", &tuning.recoveryTime, 0.5f, 10.0f, "%.1f s");
    HoverTooltip("撃つ合図から頭上円陣へ戻す合図までの秒数。短いと、飛んでいる途中で円陣へ切り替わる");
    ImGui::TextDisabled("予告 %.1f 秒（並ぶ＋構える）→ 撃つ → %.1f 秒で戻す",
                        tuning.fanFormTime + tuning.fanHoldTime, tuning.recoveryTime);

    if (ImGui::Button("既定値に戻す", ImVec2(140, 24)))
        tuning = SquadTuning{};

    ImGui::TreePop();
}



// DroneCommandSystem の後、DroneFormationSystem の前
REGISTER_LOGIC_SYSTEM(SquadUpdateSystem, Priority::LogicStage::L02_Update)
