#include "PlayerRushSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/TimeService.h"
#include "Game/Core/GameEvents.h"
#include "Game/Logics/Character/Player/Dash/PlayerDashComponent.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Game/Logics/Combat/Components/DeadTag.h"
#include "Game/Logics/Combat/Components/CombatComponents.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Animation/Core/AnimStateMachine.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// 回避離脱のトリガーと、向いている方から見た跳ぶ向き（X は右が正、Y は前が正）
// Rush_Evade は X / Y で前後左右の回避を混ぜるブレンドスペース
static const uint32_t HASH_TRIG_RUSH_EVADE    = CCL::Utils::HashString("RushEvade");
static const uint32_t HASH_PARAM_RUSH_EVADE_X = CCL::Utils::HashString("RushEvadeX");
static const uint32_t HASH_PARAM_RUSH_EVADE_Y = CCL::Utils::HashString("RushEvadeY");

// 詰め始めのトリガー。AnyState から突っ込む構え（RushDiveStart）へ入る
static const uint32_t HASH_TRIG_RUSH_CLOSE_IN = CCL::Utils::HashString("RushCloseIn");

// 詰めの斬りのトリガー。AnyState から着地の斬り（RushDiveEnd）へ入る
static const uint32_t HASH_TRIG_RUSH_STRIKE = CCL::Utils::HashString("RushStrike");

// ラッシュ中か。詰めきれずに終わったとき、突っ込む姿勢のループから待機へ戻すのに使う
static const uint32_t HASH_PARAM_IS_RUSHING = CCL::Utils::HashString("IsRushing");

// 連撃の斬りのトリガー。押すたびに Rush_Slash_A と B を交互に頭から出し直す
static const uint32_t HASH_TRIG_RUSH_ATTACK = CCL::Utils::HashString("RushAttack");

// とどめのトリガー。斬りの途中から RushFinish へ移る
static const uint32_t HASH_TRIG_RUSH_FINISH = CCL::Utils::HashString("RushFinish");

// 連撃中にいてよいアニメのステート。ここから抜けたら連撃を終える
static const uint32_t HASH_STATE_RUSH_DIVE_END = CCL::Utils::HashString("Rush_Dive_End");
static const uint32_t HASH_STATE_RUSH_SLASH_A  = CCL::Utils::HashString("Rush_Slash_A");
static const uint32_t HASH_STATE_RUSH_SLASH_B  = CCL::Utils::HashString("Rush_Slash_B");
static const uint32_t HASH_STATE_RUSH_FINISH   = CCL::Utils::HashString("Rush_Finish");



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

std::vector<CCL::ECS::TypeID> PlayerRushSystem::GetWriteTypes() const
{
    auto types = Base::GetWriteTypes();
    types.push_back(TypeInfo<PlayerDashComponent>::ID()); // 開始時にダッシュを打ち切る
    return types;
}



std::vector<CCL::ECS::TypeID> PlayerRushSystem::GetReadTypes() const
{
    auto types = Base::GetReadTypes();
    types.push_back(TypeInfo<AnimStateMachineComponent>::ID()); // 連撃の終わりをアニメのステートで決める
    types.push_back(TypeInfo<HitboxComponent>::ID());           // 避けた攻撃の持ち主を、ラッシュの相手にする
    return types;
}



void PlayerRushSystem::Initialize()
{
    // ジャスト回避の成立を購読し、始めるプレイヤーを溜める
    // 成立は別の System のタスク上で届くので、その場では始めず Update で始める
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        m_pendingStarts   = ev.playerID;
        m_pendingAttacked = ev.attackerID; // この持ち主がラッシュの相手になる
    });

    // 攻撃の命中を購読し、当てた側を溜める。とどめが当たった瞬間を知るため
    // ここでは誰の命中かを見ず、Update でラッシュ中のプレイヤーと照らし合わせる
    ListenEvent<AttackHitConfirmedEvent>([this](const AttackHitConfirmedEvent &ev) {
        m_hitAttackers.push_back(ev.attackerID);
    });
}



void PlayerRushSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 届いたジャスト回避の成立から、相手が範囲内ならラッシュを始める
    // 2. ラッシュ中のプレイヤーを1ステップ進める

    // ---------------------------------------------------------
    // 1. 開始
    // ---------------------------------------------------------

    // ラッシュに入った瞬間の処理を行う
    if (m_pendingStarts != CCL::ECS::InvalidEntityID){
        // 必要なコンポーネントをそろえる
        auto *rush     = m_world->GetComponent<PlayerRushComponent>(m_pendingStarts);
        auto *dash     = m_world->GetComponent<PlayerDashComponent>(m_pendingStarts);
        auto *movement = m_world->GetComponent<CharacterMovementComponent>(m_pendingStarts);
        auto *player   = m_world->GetComponent<PlayerComponent>(m_pendingStarts);
        auto *anim     = m_world->GetComponent<AnimParametersComponent>(m_pendingStarts);
        auto *trans    = m_world->GetComponent<TransformComponent>(m_pendingStarts);
        if (rush && dash && movement && player && anim && trans)
        {
            // 相手はロックオンでなく、避けた攻撃で決める
            // ロックオンした敵と別の敵の攻撃を避けたときも、攻撃してきた方へ斬りかかる
            const EntityID target      = PickRushTarget(m_pendingAttacked);
            const auto    *targetTrans = m_world->GetComponent<TransformComponent>(target);

            // 相手が遠ければラッシュを始めず、ジャスト回避だけにする。ダッシュは打ち切らずに続く
            // 相手が分からないときは、これまで通り始めて正面へ斬る
            const bool inRange = !targetTrans ||
                                 IsInRushStartRange(*rush, trans->GetWorldPosition(), targetTrans->GetWorldPosition());
            if (inRange)
            {
                StartRush(*rush, *dash, *movement, target);
                BeginEvadeAnim(*rush, *player, *anim, trans->GetWorldPosition());
            }
            else
            {
                // 成功した手応えは残すため、世界だけを短くスローにする
                StartEvadeSlow(*rush);
            }
        }
    }
    m_pendingStarts   = CCL::ECS::InvalidEntityID;
    m_pendingAttacked = CCL::ECS::InvalidEntityID;

    // ---------------------------------------------------------
    // 2. ラッシュ中の更新
    // ---------------------------------------------------------

    const TimeContext *timeCtx;
    if (m_world->HasResource<TimeContext>())
    {
        timeCtx = &m_world->GetResource<TimeContext>();
    }
    else
    {
        timeCtx = nullptr;
    }


    ForEachWithID([&](EntityID                            id,
                      PlayerRushComponent                &rush,
                      TimeStateComponent                 &time,
                      CharacterMovementComponent         &movement,
                      PlayerComponent                    &player,
                      AnimParametersComponent            &animParams,
                      const PlayerInputComponent         &input,
                      const TransformComponent           & /*transform*/,
                      const JoltCharacterHandleComponent &joltHandle) {

        // ラッシュを終えたステップの値は、次のステップでグラフへ届く
        animParams.SetBool(HASH_PARAM_IS_RUSHING, rush.isActive);

        // ラッシュ中じゃないならスローが戻るまでの間だけプレイヤーを実時間で動かす
        if (!rush.isActive)
        {
            if (rush.evadeSlowTimer > 0.0f)
            {
                // 範囲外の回避の短いスロー。最初はプレイヤーも一緒に遅く、そこから実時間へ戻す
                // 時間切れになったら、ラッシュの後と同じ戻りへ移る
                DriveEvadeSlow(rush, time, timeCtx);
                rush.evadeSlowTimer -= dt;
                if (rush.evadeSlowTimer <= 0.0f)
                {
                    rush.evadeSlowTimer = 0.0f;
                    rush.isRecovering   = true;
                }
            }
            else if (rush.isRecovering)
            {
                // 終えた後も、世界のスローが戻りきるまではプレイヤーだけを実時間で動かす
                DriveRecovery(rush, time, timeCtx);
            }
            return;
        }

        // --- とどめの命中 ---

        // プレイヤーがラッシュ中に攻撃を当てたかを探す
        const bool hitThisStep = std::find(m_hitAttackers.begin(), m_hitAttackers.end(), id) != m_hitAttackers.end();

        // プレイヤーが攻撃を当てたかつラッシュのアニメーションなら止めの瞬間
        const bool finishHitThisStep = hitThisStep && rush.lastAnimState == HASH_STATE_RUSH_FINISH;

        // 当たった最初の1回だけ、世界と一緒にスローへ入る
        if (finishHitThisStep && !rush.finishHit) StartFinishSlow(rush, time);

        // 当たるまでは、世界のスローを保ちプレイヤーだけ実時間にする
        if (!rush.finishHit) DriveTimeScale(rush, time, timeCtx);

        // ラッシュ中にプレイヤーを無敵にする
        MaintainInvincibility(id, rush);

        // --- フェーズごとの操作 ---

        switch (rush.phase)
        {
        case RushPhase::Locked:
            // 離れる移動は StartRush で予約してあり、物理が運ぶ
            // 運び終えたら待機へ進める。終点で速度が 0 になるので滑らない
            HoldEvadeFacing(rush, player);
            if (!movement.IsMoving()) AdvancePhase(rush);
            break;

        case RushPhase::Ready:
            // 足を止めて、攻撃の入力を待つ
            HoldEvadeFacing(rush, player);
            movement.desiredVelocity = Vector3::Zero;
            if (input.attackPressed) EnterAttack(rush, player, animParams);
            break;

        case RushPhase::Attack:
            // 通常攻撃は PlayerAttackSystem が止めているので、詰めの斬りはここで出す
            DriveCloseIn(rush, movement, player, animParams, GetCharacterWorldPosition(joltHandle));

            // 斬った後に押したら、残り時間を待たずに連撃へ移る
            if (rush.attackFired && input.attackPressed)
            {
                EnterFlurry(rush);
                RequestNextStep(rush, player, animParams, GetCharacterWorldPosition(joltHandle));
            }
            break;

        case RushPhase::Flurry:
            // 足を止め、押すたびに次の斬りを頼む
            movement.desiredVelocity = Vector3::Zero;
            if (input.attackPressed)
            {
                RequestNextStep(rush, player, animParams, GetCharacterWorldPosition(joltHandle));
            }

            TrackRushAnim(rush, m_world->GetComponent<AnimStateMachineComponent>(id));
            break;
        }

        // --- フェーズの時間 ---

        // 回避離脱は移動の終わりで進めるので、時間では数えない
        // 待機から攻撃へ移ったステップは、攻撃の持ち時間から減る
        if (rush.phase != RushPhase::Locked)
        {
            rush.timer -= dt;
            if (rush.timer <= 0.0f) AdvancePhase(rush);
        }

        // 画面演出は PlayerRushEffectSystem が isActive を直接読む
        // 終えたステップは isActive が false なので、そのステップから演出も消え始める
    });

    // 命中はこのステップで照らし合わせ終えたので捨てる
    m_hitAttackers.clear();
}



// ---------------------------------------------------------------------------
// フェーズ
// ---------------------------------------------------------------------------

void PlayerRushSystem::StartRush(PlayerRushComponent &rush, PlayerDashComponent &dash, CharacterMovementComponent &movement,
                                 CCL::ECS::EntityID target)
{
    // ラッシュの状態の初期化
    rush.closeInTarget  = target;
    rush.isActive       = true;
    rush.isRecovering   = false; // 前のラッシュの戻りの途中でも、ここから仕切り直す
    rush.evadeSlowTimer = 0.0f;  // 範囲外の回避のスローの途中でも、ラッシュのスローに切り替える
    rush.slowWorldScale = rush.worldScale;
    rush.phase         = RushPhase::Locked;
    rush.timer         = 0.0f;
    rush.attackFired   = false;
    rush.rushStep      = 0;
    rush.finishEntered = false;
    rush.finishHit     = false;
    rush.lastAnimState = 0;
    rush.evadeDir      = dash.dashDirection;

    // ダッシュを打ち切る。無敵はラッシュ側で保つ
    dash.isDashing               = false;
    dash.isInvincible            = false;
    dash.isJustEvadeWindowActive = false;
    dash.currentTimer            = dash.dashCooldown;

    // ジャスト回避の向きに移動する処理の予約
    movement.MoveInDirection(rush.evadeDir, rush.leapSpeed, rush.lockedDuration, 0.0f);
}



void PlayerRushSystem::BeginEvadeAnim(PlayerRushComponent &rush, PlayerComponent &player,
                                      AnimParametersComponent &animParams, const Vector3 &playerPos)
{
    // 処理フロー:
    // 1. 向く方を決める
    // 2. 跳ぶ向きを、向いている方から見た左右と前後の量に直す
    // 3. 回避のアニメを出す

    // ---------------------------------------------------------
    // 1. 向く方
    // ---------------------------------------------------------

    // ダッシュ中は進む方へ向きかけているが、ジャスト回避では相手を正面に置いたまま跳ぶ
    // 相手がいなければ今の向きのまま
    rush.evadeFaceYaw = player.currentYaw;

    if (IsCloseInTargetAlive(rush.closeInTarget))
    {
        if (const auto *targetTrans = m_world->GetComponent<TransformComponent>(rush.closeInTarget))
        {
            Vector3 to = Vector3(targetTrans->GetWorldPosition()) - playerPos;
            to.y       = 0.0f;
            if (to.LengthSquared() > 1e-6f) rush.evadeFaceYaw = std::atan2(to.x, to.z);
        }
    }

    HoldEvadeFacing(rush, player);

    // ---------------------------------------------------------
    // 2. 左右と前後の量
    // ---------------------------------------------------------

    // 左手系なので right = up × forward
    const Vector3 forward(std::sin(rush.evadeFaceYaw), 0.0f, std::cos(rush.evadeFaceYaw));
    const Vector3 right(forward.z, 0.0f, -forward.x);

    float x = rush.evadeDir.Dot(right);
    float y = rush.evadeDir.Dot(forward);

    // 向きがないとき（テストで始めたときなど）は前へ跳んだことにする
    if (x * x + y * y < 1e-4f)
    {
        x = 0.0f;
        y = 1.0f;
    }

    // ---------------------------------------------------------
    // 3. アニメ
    // ---------------------------------------------------------

    // 入った瞬間の重みはこの値で決まるので、トリガーより先に書く
    animParams.SetFloat(HASH_PARAM_RUSH_EVADE_X, x);
    animParams.SetFloat(HASH_PARAM_RUSH_EVADE_Y, y);
    animParams.SetTrigger(HASH_TRIG_RUSH_EVADE);
}



void PlayerRushSystem::HoldEvadeFacing(const PlayerRushComponent &rush, PlayerComponent &player)
{
    // 攻撃の向き直りを借りる。移動の向きより優先されるので、跳ぶ方へ向き直らない
    player.attackFaceYaw   = rush.evadeFaceYaw;
    player.attackFaceTimer = 0.1f;
}



void PlayerRushSystem::AdvancePhase(PlayerRushComponent &rush)
{
    switch (rush.phase)
    {
    case RushPhase::Locked:
        rush.phase = RushPhase::Ready;
        rush.timer = rush.readyDuration;
        break;

    case RushPhase::Ready:
        // 攻撃しないまま時間切れ
        EndRush(rush);
        break;

    case RushPhase::Attack:
        if (!rush.attackFired)
        {
            // 詰めきれずに時間切れ。壁に阻まれたときなど
            EndRush(rush);
        }
        else
        {
            // 斬った後、押さないまま時間が過ぎた。連撃へ移り、押せば次の斬りが出る
            EnterFlurry(rush);
        }
        break;

    case RushPhase::Flurry:
        // アニメが最後まで流れたか、とどめを出し終えたか、上限に達した
        EndRush(rush);
        break;
    }
}



void PlayerRushSystem::EnterFlurry(PlayerRushComponent &rush)
{
    rush.phase = RushPhase::Flurry;
    rush.timer = rush.flurryMaxDuration;
}



void PlayerRushSystem::TrackRushAnim(PlayerRushComponent &rush, const AnimStateMachineComponent *fsm)
{
    const uint32_t state = fsm ? fsm->currentStateHash : 0u;

    // 斬りのステートに入った瞬間だけ数える
    // 押した回数で数えると、受付区間の前の連打まで数えてしまう
    if (state != rush.lastAnimState)
    {
        if (state == HASH_STATE_RUSH_SLASH_A || state == HASH_STATE_RUSH_SLASH_B)
        {
            ++rush.rushStep;
        }
        if (state == HASH_STATE_RUSH_FINISH)
        {
            rush.finishEntered = true; // 終えるときに、スローの戻りを長く取る
        }
        rush.lastAnimState = state;
    }

    // 受付区間で押されないまま流れきると、アニメグラフが待機などへ戻す
    const bool inRushAnim = state == HASH_STATE_RUSH_DIVE_END || state == HASH_STATE_RUSH_SLASH_A ||
                            state == HASH_STATE_RUSH_SLASH_B || state == HASH_STATE_RUSH_FINISH;
    if (!inRushAnim)
    {
        rush.timer = 0.0f;
    }
}



void PlayerRushSystem::EnterAttack(PlayerRushComponent &rush, const PlayerComponent &player,
                                   AnimParametersComponent &animParams)
{
    rush.phase       = RushPhase::Attack;
    rush.timer       = rush.attackDuration;
    rush.attackFired = false;

    // 詰め先は StartRush で決めてある
    // 相手がいないときは、今向いている方へ詰める。yaw 0 が +Z
    const float yaw = player.currentYaw;
    rush.closeInDir = Vector3(std::sin(yaw), 0.0f, std::cos(yaw));

    // 突っ込む構えに入る。すでに間合いの中なら、同じステップの FireStrike が取り消す
    animParams.SetTrigger(HASH_TRIG_RUSH_CLOSE_IN, 0.1f);
}



// ---------------------------------------------------------------------------
// 時間
// ---------------------------------------------------------------------------

void PlayerRushSystem::DriveTimeScale(PlayerRushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx)
{
    // 毎ステップ要求し続けて、世界を slowWorldScale の遅さに保つ
    // slowWorldScale はラッシュなら worldScale、範囲外の回避なら evadeSlowScale
    Time::SustainWorldSlow(*m_world, rush.slowWorldScale, rush.slowRecover);

    // localDt = rawDt × 世界の倍率 × cinematicScale なので、
    // cinematicScale を playerScale ÷ 世界の倍率にすると、プレイヤーだけ playerScale で進む

    // 割る数は slowWorldScale より下げない
    // ほかのスローで世界がさらに遅くなっても、cinematicScale が大きくなりすぎない
    const float worldFloor = (std::max)(CurrentWorldScale(time, timeCtx), rush.slowWorldScale);

    time.cinematicScale = rush.playerScale / worldFloor;
}



void PlayerRushSystem::DriveRecovery(PlayerRushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx)
{
    // 終えた瞬間に補正を外すと、プレイヤーも世界と同じスローに落ちる
    // とどめの後に、体だけが一瞬スローモーションになってしまう
    const float currentWorld = CurrentWorldScale(time, timeCtx);

    // 世界が等速に戻ったら、補正を外して終える
    if (currentWorld >= 0.999f)
    {
        time.cinematicScale = 1.0f;
        rush.isRecovering   = false;
        return;
    }

    // プレイヤーの倍率も、世界の戻り具合に合わせて playerScale から 1 へ寄せる
    // 世界が戻りきった瞬間に、プレイヤーの速さが跳ばないようにする
    // 戻る元の遅さは、かけていたスロー（ラッシュか範囲外の回避か）で決まる
    const float worldFloor = (std::max)(currentWorld, rush.slowWorldScale);
    const float progress   = std::clamp((worldFloor - rush.slowWorldScale) / (std::max)(1.0f - rush.slowWorldScale, 1e-4f), 0.0f, 1.0f);
    const float playerRate = std::lerp(rush.playerScale, 1.0f, progress);

    time.cinematicScale = playerRate / worldFloor;
}



float PlayerRushSystem::CurrentWorldScale(const TimeStateComponent &time, const TimeContext *timeCtx) const
{
    if (!timeCtx) return 1.0f;
    return timeCtx->globalScale * timeCtx->groupScales[static_cast<size_t>(time.group)];
}



// 範囲外のジャスト回避で、世界だけの短いスローを始める
void PlayerRushSystem::StartEvadeSlow(PlayerRushComponent &rush)
{
    // ダッシュには触らない。打ち切らずに、そのまま最後まで走らせる
    // スローの間と戻りの間は、ラッシュと同じ DriveTimeScale / DriveRecovery でプレイヤーを実時間にする
    rush.evadeSlowTimer = rush.evadeSlowDuration;
    rush.slowWorldScale = rush.evadeSlowScale;
    rush.isRecovering   = false; // 前のスローの戻りの途中でも、ここから仕切り直す
}



// 範囲外の回避のスローを1ステップ進める。最初はプレイヤーも世界と一緒に遅くする
void PlayerRushSystem::DriveEvadeSlow(PlayerRushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx)
{
    // 世界を遅く保ち、プレイヤーを実時間にする補正を入れる（ラッシュと同じ処理）
    DriveTimeScale(rush, time, timeCtx);

    // 成立してから evadeSharedSlowTime の間は、その補正を弱める
    // cinematicScale が 1 なら世界と同じ遅さ、DriveTimeScale の値なら実時間。その間を寄せる
    // 2乗で寄せるので、最初は一緒に遅いまま止まって見え、後半で自分だけ速くなる
    if (rush.evadeSharedSlowTime <= 0.0f) return;

    const float elapsed = rush.evadeSlowDuration - rush.evadeSlowTimer;
    if (elapsed >= rush.evadeSharedSlowTime) return;

    // スローの途中でインスペクタから長さを縮めると elapsed が負になるので、0 で止める
    const float progress = (std::max)(elapsed, 0.0f) / rush.evadeSharedSlowTime;
    time.cinematicScale  = std::lerp(1.0f, time.cinematicScale, progress * progress);
}



// とどめの命中で、プレイヤーも世界と一緒のスローに落とす
void PlayerRushSystem::StartFinishSlow(PlayerRushComponent &rush, TimeStateComponent &time)
{
    rush.finishHit = true;

    // プレイヤーだけ実時間にする補正を外す。これでプレイヤーも世界の倍率で進む
    time.cinematicScale = 1.0f;

    // 世界のスローの要求はこれが最後。以降は finishRecover かけて等速へ戻る
    Time::SustainWorldSlow(*m_world, rush.worldScale, rush.finishRecover);
}



void PlayerRushSystem::EndRush(PlayerRushComponent &rush)
{
    rush.isActive = false;
    rush.timer    = 0.0f;

    // とどめが当たっていれば、スローは当たった瞬間から戻り始めている
    // プレイヤーも世界と同じ時計で動いているので、戻りの補正はいらない
    if (rush.finishHit)
    {
        rush.isRecovering = false;
        return;
    }

    rush.isRecovering = true;

    // 要求をやめれば、世界のスローは slowRecover かけて等速へ戻る
    // とどめで終えたときは、余韻を残すためにもっとゆっくり戻す
    if (rush.finishEntered && rush.finishRecover > rush.slowRecover)
    {
        Time::SustainWorldSlow(*m_world, rush.worldScale, rush.finishRecover);
    }
}



// ---------------------------------------------------------------------------
// 間合い詰め・連撃・無敵
// ---------------------------------------------------------------------------

void PlayerRushSystem::DriveCloseIn(PlayerRushComponent &rush, CharacterMovementComponent &movement,
                                    PlayerComponent &player, AnimParametersComponent &animParams,
                                    const Vector3 &playerPos)
{
    // 斬った後の移動は、攻撃アニメのルートモーションに任せる
    if (rush.attackFired) return;

    // --- 詰める向きと、斬るか ---

    // 詰め先がいなければ、その場で正面へ斬る
    Vector3 dir          = rush.closeInDir;
    bool    reachedRange = true;

    // 相手がいるときは、動いても追えるように毎ステップ向け直す
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

    // 詰める向きへ向かせる。体を回すのは PlayerFacingSystem
    player.attackFaceYaw   = std::atan2(dir.x, dir.z);
    player.attackFaceTimer = 0.1f;

    // --- 斬る・詰める ---

    if (reachedRange)
    {
        FireStrike(rush, player, animParams);
        return;
    }

    // 詰めている間は攻撃アニメが流れない
    // ルートモーションに邪魔されないので、overrideSpeed が効く
    movement.desiredVelocity = dir;
    movement.overrideSpeed   = rush.closeInSpeed;
}



void PlayerRushSystem::FireStrike(PlayerRushComponent &rush, PlayerComponent &player, AnimParametersComponent &animParams)
{
    rush.attackFired = true;
    rush.rushStep    = 0; // 詰めの斬りは数えない

    // 連撃へ移るまでの時間は、斬った瞬間から数え直す
    // 詰めにかかった時間に左右されないようにするため
    rush.timer = rush.attackDuration;

    player.attackFaceYaw   = std::atan2(rush.closeInDir.x, rush.closeInDir.z);
    player.attackFaceTimer = 0.22f;

    // 詰め始めの合図が残っていると、斬った後に構えへ引き戻される
    animParams.ConsumeTrigger(HASH_TRIG_RUSH_CLOSE_IN);

    // AnyState から入るので、構えでも突っ込む姿勢でも、詰めずに斬るときでも出せる
    animParams.SetTrigger(HASH_TRIG_RUSH_STRIKE, 0.25f);
}



void PlayerRushSystem::RequestNextStep(PlayerRushComponent &rush, PlayerComponent &player,
                                       AnimParametersComponent &animParams, const Vector3 &playerPos)
{
    // トリガーは rushInputBuffer の間残り、次の受付区間でアニメが進む
    // 斬りの数はここでは数えない。入ったかどうかは TrackRushAnim がアニメで数える
    const bool finish = rush.rushStep >= rush.rushStepCount;
    animParams.SetTrigger(finish ? HASH_TRIG_RUSH_FINISH : HASH_TRIG_RUSH_ATTACK, rush.rushInputBuffer);

    // 詰め先がいれば、その方へ向かせる
    if (IsCloseInTargetAlive(rush.closeInTarget))
    {
        if (const auto *enemyTrans = m_world->GetComponent<TransformComponent>(rush.closeInTarget))
        {
            Vector3 to = Vector3(enemyTrans->GetWorldPosition()) - playerPos;
            to.y       = 0.0f;
            if (to.LengthSquared() > 1e-6f)
            {
                player.attackFaceYaw   = std::atan2(to.x, to.z);
                player.attackFaceTimer = 0.22f;
            }
        }
    }
}



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

// 避けた攻撃の持ち主を、ラッシュの相手として返す
EntityID PlayerRushSystem::PickRushTarget(EntityID attackedID) const
{
    if (!m_world->IsEntityValid(attackedID)) return InvalidEntityID;

    // 剣の判定もドローンも HitboxComponent を持ち、ownerID が攻撃した本体（ボスなど）
    // 持っていなければ、避けた物そのものを相手にする（デバッグの発行は敵本体を渡す）
    EntityID target = attackedID;
    if (const auto *hitbox = m_world->GetComponent<HitboxComponent>(attackedID))
    {
        if (hitbox->ownerID != InvalidEntityID) target = hitbox->ownerID;
    }

    if (!IsCloseInTargetAlive(target)) return InvalidEntityID;
    return target;
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
    case RushPhase::Flurry:
        return "Flurry (連撃)";
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
        else if (rush->evadeSlowTimer > 0.0f)
        {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "EVADE SLOW (範囲外)  残り %.2f s", rush->evadeSlowTimer);
        }
        else
        {
            ImGui::TextDisabled("inactive");
        }
        ImGui::Text("phase timer : %.3f s", rush->timer);
        if (rush->isActive)
        {
            // 数えた斬りが上限に届いていれば、次に押すととどめ
            ImGui::Text("rush step   : %d / %d%s", rush->rushStep, rush->rushStepCount,
                        rush->rushStep >= rush->rushStepCount ? "  (次はとどめ)" : "");
        }

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
            auto *player   = m_world->GetComponent<PlayerComponent>(id);
            auto *anim     = m_world->GetComponent<AnimParametersComponent>(id);
            auto *trans    = m_world->GetComponent<TransformComponent>(id);
            if (dash && movement && player && anim && trans)
            {
                // ダッシュの向きがなければ、今向いている方へ離れる
                if (Vector3(dash->dashDirection).LengthSquared() < 1e-4f)
                {
                    if (const auto *player = m_world->GetComponent<PlayerComponent>(id))
                    {
                        dash->dashDirection =
                            Vector3(std::sin(player->currentYaw), 0.0f, std::cos(player->currentYaw));
                    }
                }
                // 避けた攻撃がないので、相手なしで正面へ斬る
                StartRush(*rush, *dash, *movement, InvalidEntityID);
                BeginEvadeAnim(*rush, *player, *anim, trans->GetWorldPosition());
            }
        }

        ImGui::PopID();
    });
}



// 入力・ダッシュと同じ L02_Update。物理より前に、移動と時間倍率を確定させる
REGISTER_LOGIC_SYSTEM(PlayerRushSystem, Priority::LogicStage::L02_Update);
