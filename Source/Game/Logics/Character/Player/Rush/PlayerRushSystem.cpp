#include "PlayerRushSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/TimeService.h"      // 世界スロー(SustainWorldSlow)
#include "Game/Core/GameEvents.h"         // JustEvadeSuccessEvent
#include "Game/Logics/Character/Player/Rush/PlayerRushEffectComponent.h" // ビネット演出の要求フラグ
#include "Game/Logics/Character/Player/Dash/PlayerDashComponent.h"   // 開始時にダッシュを中断する
#include "Game/Logics/Combat/Components/HealthComponent.h"          // 無敵タイマーを維持する
#include "Game/Logics/Combat/Components/DeadTag.h"                  // 詰め先が死んでいないか判定
#include "Engine/Core/Math/StringHash.h"                           // 斬りトリガーのハッシュ

#include <imgui.h> // デバッグ UI(OnGui)

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// 斬りのアニメトリガー。通常攻撃(PlayerAttackSystem)と同じ名前で、どの斬りになるかはアニメグラフが決める。
static const uint32_t HASH_TRIG_ATTACK = CCL::Utils::HashString("Attack");

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

std::vector<CCL::ECS::TypeID> PlayerRushSystem::GetWriteTypes() const
{
    // 画面演出のシングルトンと、開始時に中断する PlayerDashComponent も書くので申告する。
    auto types = Base::GetWriteTypes();
    types.push_back(TypeInfo<PlayerRushEffectComponent>::ID());
    types.push_back(TypeInfo<PlayerDashComponent>::ID());
    return types;
}

void PlayerRushSystem::Initialize()
{
    // ジャスト回避の成功を購読する。
    // 成功は別システム(PlayerJustEvadeSystem)のタスク上で届くので、ここでは溜めるだけにして、
    // 実際の開始は Update で行う。
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        m_pendingStarts.push_back(ev.playerID);
    });
}

void PlayerRushSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 届いたジャスト回避成功を受けて、ダッシュを中断しラッシュを開始する
    // 2. ラッシュ中はスロー維持・無敵維持・フェーズごとの操作を行い、時間でフェーズを進める
    // 3. ラッシュ中かどうかを画面演出（ビネット）へ伝える

    // ---------------------------------------------------------
    // 1. ラッシュの開始
    // ---------------------------------------------------------
    for (EntityID id : m_pendingStarts)
    {
        auto *rush = m_world->GetComponent<PlayerRushComponent>(id);
        auto *dash = m_world->GetComponent<PlayerDashComponent>(id);
        if (rush && dash)
        {
            StartRush(*rush, *dash);
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

    ForEachWithID([&](EntityID                    id,
                      PlayerRushComponent              &rush,
                      TimeStateComponent         &time,
                      CharacterMovementComponent &movement,
                      PlayerComponent            &player,
                      AnimParametersComponent    &animParams,
                      const PlayerInputComponent &input,
                      const TransformComponent   &transform) {
        if (!rush.isActive)
        {
            return;
        }

        // 世界のタイムスケールをスローにして、
        // プレイヤーは実時間で動くようにする
        DriveTimeScale(rush, time, timeCtx);

        // ジャスト回避状態は無敵状態を維持する
        MaintainInvincibility(id, rush);

        // --- フェーズごとの操作 ---
        switch (rush.phase)
        {
        case RushPhase::Locked:
            // 回避離脱。操作不能。ダッシュ方向へ前進する。
            DriveLeap(rush, movement);
            break;

        case RushPhase::Ready:
            // 操作不能。攻撃入力があれば攻撃状態へ移る。
            StopPlayer(movement);
            if (input.attackPressed)
            {
                EnterAttack(rush, player);
            }
            break;

        case RushPhase::Attack:
            // 詰め → 斬りまでをここで完結させる（通常攻撃は PlayerAttackSystem 側で抑止している）。
            DriveCloseIn(rush, movement, player, animParams, Vector3(transform.position));
            break;

        case RushPhase::Free:
            // 自由操作。移動は上書きしない（ダッシュ/歩走・攻撃がそのまま効く）。
            break;
        }

        // --- フェーズの時間を進める ---
        // 待機中に攻撃へ移った場合は phase が Attack に変わり timer も入れ替わっているので、
        // そのまま攻撃の持ち時間として減る。
        rush.timer -= dt;
        if (rush.timer <= 0.0f)
        {
            AdvancePhase(rush, time);
        }

        // 終了したフレームは isActive が false になるので、そのフレームから演出も降り始める。
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

void PlayerRushSystem::StartRush(PlayerRushComponent &rush, PlayerDashComponent &dash)
{
    rush.isActive    = true;
    rush.phase       = RushPhase::Locked;
    rush.timer       = rush.lockedDuration;
    rush.attackFired = false;               // 前のラッシュの斬り済みを持ち越さない
    rush.evadeDir    = dash.dashDirection;  // ダッシュした方向へ離脱する

    // ダッシュのままにせず中断する。以降は回避離脱（Locked）が移動を駆動する。
    dash.isDashing               = false;
    dash.isInvincible            = false; // 無敵はラッシュ側で維持する
    dash.isJustEvadeWindowActive = false;
    dash.currentTimer            = dash.dashCooldown;
}

void PlayerRushSystem::AdvancePhase(PlayerRushComponent &rush, TimeStateComponent &time)
{
    switch (rush.phase)
    {
    case RushPhase::Locked:
        rush.phase = RushPhase::Ready;
        rush.timer = rush.readyDuration;
        break;

    case RushPhase::Ready:
        // 待機のまま時間切れ＝攻撃しなかった。自由へ移る。
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

void PlayerRushSystem::EnterAttack(PlayerRushComponent &rush, const PlayerComponent &player)
{
    rush.phase       = RushPhase::Attack;
    rush.timer       = rush.attackDuration;
    rush.attackFired = false; // 詰め切ってから1回だけ斬る

    // 詰め先：ロックオン中の敵。無ければ今キャラが向いている方向へ詰める。
    rush.closeInTarget = PickCloseInTarget(player);

    const float yaw  = player.currentYaw;
    rush.closeInDir  = Vector3(std::sin(yaw), 0.0f, std::cos(yaw)); // 0 が +Z の規約
}

// ---------------------------------------------------------------------------
// 時間制御
// ---------------------------------------------------------------------------

void PlayerRushSystem::DriveTimeScale(PlayerRushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx)
{
    // 世界を worldScale まで遅くする。毎フレーム要求し続けることで、その遅さを維持する。
    Time::SustainWorldSlow(*m_world, rush.worldScale, rush.slowRecover);

    // プレイヤーだけ実時間へ戻す。
    // localDt = rawDt × worldScale × cinematicScale なので、cinematicScale = playerScale / worldScale と
    // すると worldScale が約分され、プレイヤーの時計だけが playerScale（既定 1.0＝実時間）で進む。
    const float currentWorld = timeCtx
                                   ? (timeCtx->globalScale * timeCtx->groupScales[static_cast<size_t>(time.group)])
                                   : 1.0f;

    // 除数の下限を worldScale で止める。
    // 他のスロー要因で世界が worldScale より深く落ちても、cinematicScale が過大にならないようにする。
    const float worldFloor = (std::max)(currentWorld, rush.worldScale);

    time.cinematicScale = rush.playerScale / worldFloor;
}

void PlayerRushSystem::EndRush(PlayerRushComponent &rush, TimeStateComponent &time)
{
    rush.isActive = false;
    rush.timer    = 0.0f;

    // 世界スローはもう要求しない（呼ぶのをやめれば slowRecover のイーズで等速へ戻る）。
    // プレイヤーのスロー打ち消しもやめる。
    time.cinematicScale = 1.0f;
}

// ---------------------------------------------------------------------------
// 移動・無敵
// ---------------------------------------------------------------------------

void PlayerRushSystem::DriveLeap(PlayerRushComponent &rush, CharacterMovementComponent &movement)
{
    // 残り時間から進行度 t(0→1) を出す（timer は lockedDuration→0 と減る）。
    const float dur = (rush.lockedDuration > 0.0001f) ? rush.lockedDuration : 0.0001f;
    const float t   = 1.0f - (rush.timer / dur);

    // EaseOut：初速 leapSpeed で出て、末尾で 0 へ減速する（(1-t)^2）。
    const float ease  = 1.0f - t;
    const float speed = rush.leapSpeed * ease * ease;

    movement.desiredVelocity = rush.evadeDir;
    movement.overrideSpeed   = speed;
}

void PlayerRushSystem::DriveCloseIn(PlayerRushComponent &rush, CharacterMovementComponent &movement,
                                    PlayerComponent &player, AnimParametersComponent &animParams,
                                    const Vector3 &playerPos)
{
    // すでに斬りを出していれば、移動は攻撃アニメ（ルートモーションの踏み込み）に任せ、何も上書きしない。
    if (rush.attackFired)
    {
        return;
    }

    // 詰め方向と、斬りに移るかの判定。
    //   ロックオン先が居る間は、毎フレームその位置へ向け直す（敵が動いても追う）。
    //   間合い（closeInStopDistance）に入ったら斬る。詰め先が無ければ、その場で斬る（前方ダッシュ斬り）。
    Vector3 dir          = rush.closeInDir;
    bool    reachedRange = true; // 詰め先が無ければ即・間合い内とみなす

    if (IsCloseInTargetAlive(rush.closeInTarget))
    {
        if (const auto *enemyTrans = m_world->GetComponent<TransformComponent>(rush.closeInTarget))
        {
            Vector3 to = Vector3(enemyTrans->position) - playerPos;
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

    // 詰め先の方へ向き直らせる（実際に回すのは PlayerJumpFacingSystem）。
    player.attackFaceYaw   = std::atan2(dir.x, dir.z);
    player.attackFaceTimer = 0.1f;

    if (reachedRange)
    {
        // 間合いに入った／詰め先が無い → 斬る。
        FireStrike(rush, player, animParams);
        return;
    }

    // まだ遠い → 詰める。詰め中は攻撃アニメが流れないので root motion は非アクティブ、overrideSpeed がそのまま効く。
    movement.desiredVelocity = dir;
    movement.overrideSpeed   = rush.closeInSpeed;
}

void PlayerRushSystem::FireStrike(PlayerRushComponent &rush, PlayerComponent &player, AnimParametersComponent &animParams)
{
    rush.attackFired = true;

    // その場で止めてから斬る。踏み込みは攻撃アニメのルートモーションに任せる。
    player.attackFaceYaw   = std::atan2(rush.closeInDir.x, rush.closeInDir.z);
    player.attackFaceTimer = 0.22f;

    animParams.SetTrigger(HASH_TRIG_ATTACK, 0.25f);
}

void PlayerRushSystem::StopPlayer(CharacterMovementComponent &movement)
{
    // 移動の意思を打ち消し、その場で止める（overrideSpeed=0 で速度を 0 に上書き）。
    movement.desiredVelocity = Vector3::Zero;
    movement.overrideSpeed   = 0.0f;
}

void PlayerRushSystem::MaintainInvincibility(EntityID id, const PlayerRushComponent &rush)
{
    // ラッシュ中は被弾しない。被弾処理が見る無敵タイマーを、毎フレーム一定以上に保つ。
    // ヒットストップ中は localDt=0 で減らないので、停止中も無敵が続く。
    if (auto *health = m_world->GetComponent<HealthComponent>(id))
    {
        if (health->invincibilityTimer < rush.invincibleRefresh)
        {
            health->invincibilityTimer = rush.invincibleRefresh;
        }
    }
}

// ---------------------------------------------------------------------------
// 詰め先の選択
// ---------------------------------------------------------------------------

CCL::ECS::EntityID PlayerRushSystem::PickCloseInTarget(const PlayerComponent &player) const
{
    // lockOnTargetID は EntityLink。EntityID へ暗黙変換して渡す。
    const EntityID id = player.lockOnTargetID;
    return IsCloseInTargetAlive(id) ? id : InvalidEntityID;
}

bool PlayerRushSystem::IsCloseInTargetAlive(CCL::ECS::EntityID id) const
{
    return id != InvalidEntityID && m_world->IsEntityValid(id) && !m_world->HasComponent<Tag::DeadTag>(id);
}

// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

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
    // 1. フェーズと残り時間・離脱方向・詰め先を出す
    // 2. 「移動の指令(desiredVelocity/overrideSpeed)」と「結果(currentVelocity)」を並べて切り分ける
    // 3. 無敵・時間倍率を出し、ジャスト回避を待たずに始められるテストボタンを置く

    m_world->ViewEach<PlayerRushComponent>([&](EntityID id) {
        auto *rush = m_world->GetComponent<PlayerRushComponent>(id);
        if (!rush)
        {
            return;
        }

        ImGui::PushID(static_cast<int>(id));
        ImGui::Text("Entity #%u", EntityHandle::GetIndex(id));

        // ---------------------------------------------------------
        // 1. フェーズ・離脱方向・詰め先
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
        // 2. 移動の指令と結果（効いていない時はここで切り分ける）
        // ---------------------------------------------------------
        if (const auto *mv = m_world->GetComponent<CharacterMovementComponent>(id))
        {
            const Vector3 dv = mv->desiredVelocity;
            ImGui::Text("desiredVelocity : (%.2f, %.2f, %.2f)", dv.x, dv.y, dv.z);
            ImGui::Text("overrideSpeed   : %.2f  (-1=上書きなし)", mv->overrideSpeed);
            ImGui::Text("currentVelocity : len %.2f m/s", Vector3(mv->currentVelocity).Length());

            // ルートモーションが動きを握っていると overrideSpeed は無視される。
            // 離脱・詰めの移動が出ないときの主因になりやすいので目立たせる。
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
        // 3. 無敵・時間倍率・テスト起動
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

        if (ImGui::Button("Force Start Rush (test)"))
        {
            // ジャスト回避の成功を待たずに開始して、離脱・詰めの挙動だけを確かめる。
            if (auto *dash = m_world->GetComponent<PlayerDashComponent>(id))
            {
                // ダッシュ方向が無ければ、今向いている方向を離脱方向にする。
                if (Vector3(dash->dashDirection).LengthSquared() < 1e-4f)
                {
                    if (const auto *player = m_world->GetComponent<PlayerComponent>(id))
                    {
                        dash->dashDirection =
                            Vector3(std::sin(player->currentYaw), 0.0f, std::cos(player->currentYaw));
                    }
                }
                StartRush(*rush, *dash);
            }
        }

        ImGui::PopID();
    });
}

// 入力・ダッシュと同じ L02_Update。物理(L03以降)より前に時間倍率を確定させる。
REGISTER_LOGIC_SYSTEM(PlayerRushSystem, Priority::LogicStage::L02_Update);
