#include "PlayerRushSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/TimeService.h"                                     // 世界のスロー
#include "Game/Core/GameEvents.h"                                        // ジャスト回避の成立
#include "Game/Logics/Character/Player/Rush/PlayerRushEffectComponent.h" // 画面演出の要求
#include "Game/Logics/Character/Player/Dash/PlayerDashComponent.h"       // 開始時にダッシュを打ち切る
#include "Game/Logics/Combat/Components/HealthComponent.h"               // 無敵タイマー
#include "Game/Logics/Combat/Components/DeadTag.h"                       // 詰め先が生きているか
#include "Engine/Core/Math/StringHash.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// 斬りのアニメのトリガー
// 通常攻撃と同じ名前で、どの斬りになるかはアニメグラフが決める
static const uint32_t HASH_TRIG_ATTACK = CCL::Utils::HashString("Attack");



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

std::vector<CCL::ECS::TypeID> PlayerRushSystem::GetWriteTypes() const
{
    auto types = Base::GetWriteTypes();
    types.push_back(TypeInfo<PlayerRushEffectComponent>::ID());
    types.push_back(TypeInfo<PlayerDashComponent>::ID());
    return types;
}



void PlayerRushSystem::Initialize()
{
    // ジャスト回避の成立を購読し、始めるプレイヤーを溜める
    // 成立は別の System のタスク上で届くので、その場では始めず Update で始める
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        m_pendingStarts.push_back(ev.playerID);
    });
}



// 届いた成立からラッシュを始め、ラッシュ中のプレイヤーを1ステップ分進める
void PlayerRushSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 届いたジャスト回避の成立から、ラッシュを始める
    // 2. ラッシュ中は、スローと無敵を保ち、フェーズごとの操作をして、フェーズを進める
    // 3. ラッシュ中かを画面演出へ伝える

    // ---------------------------------------------------------
    // 1. ラッシュの開始
    // ---------------------------------------------------------

    for (EntityID id : m_pendingStarts)
    {
        auto *rush     = m_world->GetComponent<PlayerRushComponent>(id);
        auto *dash     = m_world->GetComponent<PlayerDashComponent>(id);
        auto *movement = m_world->GetComponent<CharacterMovementComponent>(id);
        if (rush && dash && movement)
        {
            StartRush(*rush, *dash, *movement);
        }
    }
    m_pendingStarts.clear();

    // ---------------------------------------------------------
    // 2. スロー・無敵・フェーズ
    // ---------------------------------------------------------

    const TimeContext *timeCtx = m_world->HasResource<TimeContext>()
                                     ? &m_world->GetResource<TimeContext>()
                                     : nullptr;

    bool anyActive = false;

    ForEachWithID([&](EntityID                            id,
                      PlayerRushComponent                &rush,
                      TimeStateComponent                 &time,
                      CharacterMovementComponent         &movement,
                      PlayerComponent                    &player,
                      AnimParametersComponent            &animParams,
                      const PlayerInputComponent         &input,
                      const TransformComponent           & /*transform*/,
                      const JoltCharacterHandleComponent &joltHandle) {
        if (!rush.isActive) return;
        if (!joltHandle.character) return;

        DriveTimeScale(rush, time, timeCtx);
        MaintainInvincibility(id, rush);

        // --- フェーズごとの操作 ---

        switch (rush.phase)
        {
        case RushPhase::Locked:
            // 回避離脱。離れる移動は StartRush で予約済みで、物理が運ぶ
            // 運び終わったら待機へ進める（終点で物理が速度を 0 にするので、滑らずに止まる）
            if (!movement.IsMoving()) AdvancePhase(rush, time);
            break;

        case RushPhase::Ready:
            // 待機。足を止め、攻撃の入力があれば攻撃へ移る
            movement.desiredVelocity = Vector3::Zero;
            if (input.attackPressed) EnterAttack(rush, player);
            break;

        case RushPhase::Attack:
            // 詰めて斬るまでを、ここで済ませる（通常攻撃は PlayerAttackSystem が止めている）
            DriveCloseIn(rush, movement, player, animParams, GetCharacterWorldPosition(joltHandle));
            break;

        case RushPhase::Free:
            // 自由。移動は上書きしない（ダッシュ・歩き・攻撃がそのまま効く）
            break;
        }

        // --- フェーズの時間 ---

        // 回避離脱は、予約した移動の終わりで進めるので数えない
        // 待機から攻撃へ移ったステップは、入れ替わった攻撃の持ち時間から減る
        if (rush.phase != RushPhase::Locked)
        {
            rush.timer -= dt;
            if (rush.timer <= 0.0f) AdvancePhase(rush, time);
        }

        // 終わったステップは isActive が false なので、そのステップから演出も降り始める
        anyActive = anyActive || rush.isActive;
    });

    // ---------------------------------------------------------
    // 3. 画面演出へ伝える
    // ---------------------------------------------------------

    if (auto *fx = m_world->GetSingletonComponent<PlayerRushEffectComponent>())
    {
        fx->rushActive = anyActive;
    }
}



// ---------------------------------------------------------------------------
// フェーズ
// ---------------------------------------------------------------------------

// ラッシュを始める。ダッシュを打ち切り、離れる移動を予約して回避離脱から始める
void PlayerRushSystem::StartRush(PlayerRushComponent &rush, PlayerDashComponent &dash, CharacterMovementComponent &movement)
{
    rush.isActive    = true;
    rush.phase       = RushPhase::Locked;
    rush.timer       = 0.0f;
    rush.attackFired = false;              // 前のラッシュの斬り済みを持ち越さない
    rush.evadeDir    = dash.dashDirection; // ダッシュした向きへ離れる

    // ダッシュを打ち切る。無敵はラッシュ側で保つ
    dash.isDashing               = false;
    dash.isInvincible            = false;
    dash.isJustEvadeWindowActive = false;
    dash.currentTimer            = dash.dashCooldown;

    // ダッシュの向きへ、初速 leapSpeed から 0 まで直線で落としながら走る移動を予約する
    // 進む距離は leapSpeed × lockedDuration ÷ 2。壁に当たれば沿って滑る
    movement.MoveInDirection(rush.evadeDir, rush.leapSpeed, rush.lockedDuration, 0.0f);
}



// 次のフェーズへ進める
void PlayerRushSystem::AdvancePhase(PlayerRushComponent &rush, TimeStateComponent &time)
{
    switch (rush.phase)
    {
    case RushPhase::Locked:
        rush.phase = RushPhase::Ready;
        rush.timer = rush.readyDuration;
        break;

    case RushPhase::Ready:
        // 攻撃しないまま時間切れ。自由へ移る
        rush.phase = RushPhase::Free;
        rush.timer = rush.freeDuration;
        break;

    case RushPhase::Attack:
        rush.phase = RushPhase::Free;
        rush.timer = rush.freeDuration;
        break;

    case RushPhase::Free:
        EndRush(rush, time);
        break;
    }
}



// 攻撃に入る。詰め先（ロックオン中の敵、無ければ今の正面）を決める
void PlayerRushSystem::EnterAttack(PlayerRushComponent &rush, const PlayerComponent &player)
{
    rush.phase       = RushPhase::Attack;
    rush.timer       = rush.attackDuration;
    rush.attackFired = false;

    rush.closeInTarget = PickCloseInTarget(player);

    // 詰め先が無いときは、今向いている方へ詰める（yaw は 0 が +Z）
    const float yaw = player.currentYaw;
    rush.closeInDir = Vector3(std::sin(yaw), 0.0f, std::cos(yaw));
}



// ---------------------------------------------------------------------------
// 時間
// ---------------------------------------------------------------------------

// 世界のスローを保ち、プレイヤーの時計だけを実時間へ戻す
void PlayerRushSystem::DriveTimeScale(PlayerRushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx)
{
    // 世界を worldScale まで遅くする
    // 毎ステップ要求し続けることで、その遅さを保つ
    Time::SustainWorldSlow(*m_world, rush.worldScale, rush.slowRecover);

    // localDt = rawDt × 世界の倍率 × cinematicScale なので、
    // cinematicScale = playerScale ÷ 世界の倍率 にすると、
    // 世界の倍率が約分され、プレイヤーだけ playerScale で進む
    const float currentWorld = timeCtx
                                   ? (timeCtx->globalScale * timeCtx->groupScales[static_cast<size_t>(time.group)])
                                   : 1.0f;

    // 割る数の下限を worldScale で止める
    // ほかのスローで世界がもっと遅くなっても、cinematicScale が大きくなりすぎない
    const float worldFloor = (std::max)(currentWorld, rush.worldScale);

    time.cinematicScale = rush.playerScale / worldFloor;
}



// ラッシュを終え、プレイヤーの時間倍率を戻す
void PlayerRushSystem::EndRush(PlayerRushComponent &rush, TimeStateComponent &time)
{
    rush.isActive = false;
    rush.timer    = 0.0f;

    // 世界のスローは、要求をやめれば slowRecover かけて等速へ戻る
    time.cinematicScale = 1.0f;
}



// ---------------------------------------------------------------------------
// 間合い詰め・無敵
// ---------------------------------------------------------------------------

// 攻撃を1ステップ分進める。詰めてから1回だけ斬る
void PlayerRushSystem::DriveCloseIn(PlayerRushComponent &rush, CharacterMovementComponent &movement,
                                    PlayerComponent &player, AnimParametersComponent &animParams,
                                    const Vector3 &playerPos)
{
    // 斬った後の移動は、攻撃アニメのルートモーションに任せる
    if (rush.attackFired) return;

    // --- 詰める向きと、斬るか ---

    // 詰め先が無ければ、その場で斬る（向いている方への斬り）
    Vector3 dir          = rush.closeInDir;
    bool    reachedRange = true;

    // ロックオン中は、毎ステップ敵の位置へ向け直す（敵が動いても追う）
    // closeInStopDistance まで近づいたら斬る
    if (IsCloseInTargetAlive(rush.closeInTarget))
    {
        if (const auto *enemyTrans = m_world->GetComponent<TransformComponent>(rush.closeInTarget))
        {
            Vector3 to = Vector3(enemyTrans->GetWorldPosition()) - playerPos;
            to.y       = 0.0f;
            const float dist = to.Length();

            reachedRange = (dist <= rush.closeInStopDistance);
            if (dist > 1e-3f)
            {
                dir             = to / dist;
                rush.closeInDir = dir;
            }
        }
    }

    // 詰める向きへ向き直らせる（回すのは PlayerJumpFacingSystem）
    player.attackFaceYaw   = std::atan2(dir.x, dir.z);
    player.attackFaceTimer = 0.1f;

    // --- 斬る・詰める ---

    if (reachedRange)
    {
        FireStrike(rush, player, animParams);
        return;
    }

    // 詰めている間は攻撃アニメが流れないので、ルートモーションに邪魔されず overrideSpeed が効く
    movement.desiredVelocity = dir;
    movement.overrideSpeed   = rush.closeInSpeed;
}



// 斬りを1回出す。踏み込みは攻撃アニメのルートモーションに任せる
void PlayerRushSystem::FireStrike(PlayerRushComponent &rush, PlayerComponent &player, AnimParametersComponent &animParams)
{
    rush.attackFired = true;

    player.attackFaceYaw   = std::atan2(rush.closeInDir.x, rush.closeInDir.z);
    player.attackFaceTimer = 0.22f;

    animParams.SetTrigger(HASH_TRIG_ATTACK, 0.25f);
}



// ラッシュ中は、被弾処理が見る無敵タイマーを invincibleRefresh 以上に保つ
void PlayerRushSystem::MaintainInvincibility(EntityID id, const PlayerRushComponent &rush)
{
    // ヒットストップ中は localDt が 0 でタイマーが減らないので、止まっている間も無敵が続く
    if (auto *health = m_world->GetComponent<HealthComponent>(id))
    {
        if (health->invincibilityTimer < rush.invincibleRefresh)
        {
            health->invincibilityTimer = rush.invincibleRefresh;
        }
    }
}



// ---------------------------------------------------------------------------
// 詰め先
// ---------------------------------------------------------------------------

// ロックオン中で生きている敵を返す。無ければ InvalidEntityID
CCL::ECS::EntityID PlayerRushSystem::PickCloseInTarget(const PlayerComponent &player) const
{
    const EntityID id = player.lockOnTargetID; // EntityLink から暗黙に変換する
    if (!IsCloseInTargetAlive(id)) return InvalidEntityID;
    return id;
}



// 詰め先が今も居て、生きているか
bool PlayerRushSystem::IsCloseInTargetAlive(CCL::ECS::EntityID id) const
{
    return id != InvalidEntityID && m_world->IsEntityValid(id) && !m_world->HasComponent<Tag::DeadTag>(id);
}



// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

// インスペクタに出すフェーズ名
const char *PlayerRushSystem::PhaseLabel(RushPhase phase) const
{
    switch (phase)
    {
    case RushPhase::Locked:
        return "Locked (回避離脱)";
    case RushPhase::Ready:
        return "Ready (攻撃待機)";
    case RushPhase::Attack:
        return "Attack (間合い詰め)";
    case RushPhase::Free:
        return "Free (自由行動)";
    }
    return "?";
}



void PlayerRushSystem::OnGui()
{
    // 処理フロー:
    // 1. フェーズと残り時間・離脱の向き・詰め先を出す
    // 2. 移動の依頼と結果を並べて出す
    // 3. 無敵・時間倍率と、ジャスト回避を待たずに始めるボタンを出す

    m_world->ViewEach<PlayerRushComponent>([&](EntityID id) {
        auto *rush = m_world->GetComponent<PlayerRushComponent>(id);
        if (!rush) return;

        ImGui::PushID(static_cast<int>(id));
        ImGui::Text("Entity #%u", EntityHandle::GetIndex(id));

        // ---------------------------------------------------------
        // 1. フェーズ・離脱の向き・詰め先
        // ---------------------------------------------------------

        if (rush->isActive)
        {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "ACTIVE   %s", PhaseLabel(rush->phase));
        }
        else
        {
            ImGui::TextDisabled("inactive");
        }
        ImGui::Text("phase timer : %.3f s", rush->timer);

        const Vector3 ev = rush->evadeDir;
        ImGui::Text("evadeDir    : (%.2f, %.2f, %.2f)  len %.2f", ev.x, ev.y, ev.z, ev.Length());

        if (rush->closeInTarget != InvalidEntityID)
        {
            const bool alive = IsCloseInTargetAlive(rush->closeInTarget);

            float dist = -1.0f;
            const auto *enemyTrans = m_world->GetComponent<TransformComponent>(rush->closeInTarget);
            const auto *selfTrans  = m_world->GetComponent<TransformComponent>(id);
            if (enemyTrans && selfTrans)
            {
                Vector3 to = Vector3(enemyTrans->position) - Vector3(selfTrans->position);
                to.y       = 0.0f;
                dist       = to.Length();
            }
            ImGui::Text("closeInTarget : #%u  %s  dist %.2f m",
                        EntityHandle::GetIndex(rush->closeInTarget), alive ? "alive" : "DEAD", dist);
        }
        else
        {
            ImGui::TextDisabled("closeInTarget : none (向き固定で詰める)");
        }

        ImGui::Separator();

        // ---------------------------------------------------------
        // 2. 移動の依頼と結果
        // ---------------------------------------------------------

        if (const auto *mv = m_world->GetComponent<CharacterMovementComponent>(id))
        {
            const Vector3 dv = mv->desiredVelocity;
            ImGui::Text("desiredVelocity : (%.2f, %.2f, %.2f)", dv.x, dv.y, dv.z);
            ImGui::Text("overrideSpeed   : %.2f  (-1=上書きなし)", mv->overrideSpeed);
            ImGui::Text("currentVelocity : len %.2f m/s", Vector3(mv->currentVelocity).Length());
            ImGui::Text("reservation     : %s", mv->IsMoving() ? "moving (離脱を予約中)" : "-");

            // ルートモーション中は overrideSpeed が効かないので、目立たせる
            // 詰めの移動が出ないときの主な原因になる
            if (mv->isRootMotionActive)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                   "isRootMotionActive : YES  (rootMotion len %.2f)",
                                   Vector3(mv->rootMotionVelocity).Length());
            }
            else
            {
                ImGui::Text("isRootMotionActive : no");
            }
        }

        // ---------------------------------------------------------
        // 3. 無敵・時間倍率・テスト開始
        // ---------------------------------------------------------

        if (const auto *health = m_world->GetComponent<HealthComponent>(id))
        {
            ImGui::Text("invincibilityTimer : %.3f s", health->invincibilityTimer);
        }
        if (const auto *time = m_world->GetComponent<TimeStateComponent>(id))
        {
            ImGui::Text("cinematicScale : %.2f   localDt : %.4f s", time->cinematicScale, time->localDt);
        }

        ImGui::Separator();

        // ジャスト回避を待たずに始め、離脱と詰めだけを確かめる
        if (ImGui::Button("Force Start Rush (test)"))
        {
            auto *dash     = m_world->GetComponent<PlayerDashComponent>(id);
            auto *movement = m_world->GetComponent<CharacterMovementComponent>(id);
            if (dash && movement)
            {
                // ダッシュの向きが無ければ、今向いている方へ離れる
                if (Vector3(dash->dashDirection).LengthSquared() < 1e-4f)
                {
                    if (const auto *player = m_world->GetComponent<PlayerComponent>(id))
                    {
                        dash->dashDirection =
                            Vector3(std::sin(player->currentYaw), 0.0f, std::cos(player->currentYaw));
                    }
                }
                StartRush(*rush, *dash, *movement);
            }
        }

        ImGui::PopID();
    });
}



// 入力・ダッシュと同じ L02_Update。物理より前に、移動と時間倍率を確定させる
REGISTER_LOGIC_SYSTEM(PlayerRushSystem, Priority::LogicStage::L02_Update);
