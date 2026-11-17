#include "Game/Logics/Tutorial/TutorialSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Time/TimeService.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"

#include "Game/Core/GameEvents.h"
#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Dash/PlayerDashComponent.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include <imgui.h>

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

namespace
{
    // --- 項目の達成に必要な量 ---
    constexpr float kWaitBattleTime = 1.0f;   // 明転を待つ時間（秒・実時間）
    constexpr float kMoveMeters     = 80.0f;  // 移動: 水平に動く距離（m）
    constexpr float kLookDegrees    = 270.0f; // 視点: カメラを回す角度（度）
    constexpr int   kDashCount      = 2;      // ダッシュ: 出す回数
    constexpr float kStrikeTimeout  = 5.0f;   // 斬り込み: 当てなくても次へ進む時間（秒・実時間）
    constexpr float kRushReadTime   = 3.0f;   // ラッシュ: 説明を読ませる時間（秒・実時間）

    // --- 本線のコンボ ---
    // アニメグラフ Atk_Combo のステート名を出る順に並べる。乱れ斬りは Atk_Combo3 を4回
    constexpr const char *kMainCombo[] = {
        "Atk_Combo1", "Atk_Combo2",
        "Atk_Combo3", "Atk_Combo3_2", "Atk_Combo3_3", "Atk_Combo3_4",
        "Atk_Combo4", "Atk_Combo5",
    };
    constexpr int kMainComboStages = static_cast<int>(std::size(kMainCombo));

    // --- 回避の補助スロー（回数は限らない。成功するまで毎回掛ける） ---
    constexpr float kAssistWarnTime   = 0.5f;  // 予告のスローを始める、当たるまでの残り（秒・ゲーム時間）
    constexpr float kAssistNowTime    = 0.07f; // 「今だ」に入る、当たるまでの残り（秒・ゲーム時間）
    constexpr float kAssistMissRadius = 2.5f;  // 最も近づいたときにこの距離以下なら「当たる」とみなす（m）

    // 世界の速さ（1.0 = 等速）。小さいほどゆっくり。ゆっくりにしたいときはこの2つだけを変える
    constexpr float kAssistWarnScale = 0.3f;  // 予告: 迫ってくるのを見せる間（約 1.4 秒）
    constexpr float kAssistNowScale  = 0.03f; // 今だ: 「今だ！」を出して押させる間（約 2.3 秒）

    // 補助を掛け続ける上限（秒・実時間）。上の2つから決まる長さに、1秒の余裕を足す
    constexpr float kAssistMaxTime = (kAssistWarnTime - kAssistNowTime) / kAssistWarnScale +
                                     kAssistNowTime / kAssistNowScale + 1.0f;
}



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void TutorialSystem::Initialize()
{
    // この System を登録するのは TutorialScene だけ。リソースがあることが「チュートリアル中」の印になる
    if (!m_world->HasResource<TutorialState>()) m_world->AddResource<TutorialState>();

    // イベントは発行元の System の中で届くので、印を立てるだけにして Update で読む
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &) {
        m_pendingEvents.justEvaded = true;
    });
    ListenEvent<AttackHitConfirmedEvent>([this](const AttackHitConfirmedEvent &e) {
        if (e.attackerID == m_playerID) ++m_pendingEvents.playerHits;
    });
    ListenEvent<PlayerDashedEvent>([this](const PlayerDashedEvent &e) {
        if (e.playerID == m_playerID) ++m_pendingEvents.dashed;
    });

    // 跳ぶのはプレイヤーだけなので、誰が跳んだかは見ない
    ListenEvent<PlayerJumpedEvent>([this](const PlayerJumpedEvent &e) {
        m_pendingEvents.jumpedStage = e.jumpCount;
    });
}



std::vector<TypeID> TutorialSystem::GetReadTypes() const
{
    // どれも ForEach を使わずに引くので、ここで申告する
    return {
        TypeInfo<DroneComponent>::ID(),            // 突撃中のドローン
        TypeInfo<TransformComponent>::ID(),        // ドローンの位置
        TypeInfo<TimeStateComponent>::ID(),        // ドローンが進んだ時間
        TypeInfo<BossCommandComponent>::ID(),      // ボスが組ませている陣形
        TypeInfo<CameraBodyTPSComponent>::ID(),    // 視点の項目のカメラ
        TypeInfo<PlayerComponent>::ID(),           // ロックオン先
        TypeInfo<AnimStateMachineComponent>::ID(), // 出た攻撃
        TypeInfo<PlayerDashComponent>::ID(),       // 補助スロー中にダッシュしたか
    };
}



void TutorialSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 教えている途中かを確かめる
    // 2. イベントの印を取り出す
    // 3. ムービー中と決着後は教えない
    // 4. 達成した後の余韻の間は、待ってから次の項目へ進める
    // 5. 今の項目を達成したかを判定する
    // 6. 達成したら余韻に入る（余韻のない項目はすぐ次へ）

    // ---------------------------------------------------------
    // 1. 教えている途中か
    // ---------------------------------------------------------
    if (!m_world->HasResource<TutorialState>()) return;
    TutorialState &state = m_world->GetResource<TutorialState>();

    // ヒントと補助の表示は、出す場面かどうかを毎フレーム決め直す
    state.showHint     = false;
    state.assistActive = false;
    state.cleared      = false;
    if (state.IsFinished()) return;

    if (!m_world->HasResource<BattleContext>()) return;
    const BattleContext &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return;
    m_playerID = battle.playerEntity;

    // ---------------------------------------------------------
    // 2. イベントの印
    // ---------------------------------------------------------

    // 印は毎フレーム取り出して消す。関係のない項目の間に立った印を、後の項目へ持ち越さないため
    const FrameEvents events = std::exchange(m_pendingEvents, FrameEvents{});

    // ---------------------------------------------------------
    // 3. ムービー中と決着後
    // ---------------------------------------------------------

    // ムービー中はプレイヤーが操作できず、決着後はもう戦わない。どちらもヒントを出しても押せない
    if (m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().GameplayFrozen())
    {
        EndAssist();

        // 止まっている間は記録しないので、明けた直後に古い位置・角度との差を足さないよう消しておく
        m_droneMotion.clear();
        m_hasLastSample = false;
        return;
    }

    const TutorialStepDef &def     = kTutorialSteps[state.step];
    const bool             hasText = def.text[0] != '\0';

    // ---------------------------------------------------------
    // 4. 余韻
    // ---------------------------------------------------------

    // 達成してすぐ次の説明へ切り替えると、できたことが目に入らない
    // 帯を満たしたまま、項目ごとの holdTime だけ待つ
    if (m_clearTimer > 0.0f)
    {
        m_clearTimer -= dt;

        state.showHint = hasText;
        state.cleared  = true;
        state.progress = 1.0f;

        if (m_clearTimer <= 0.0f) Advance(state);
        return;
    }

    // ---------------------------------------------------------
    // 5. 達成の判定
    // ---------------------------------------------------------
    m_stepTimer += dt;

    // 量を数えない項目は帯が空のまま。達成した瞬間に余韻で満たされる
    // ヒントは文言のある項目で出す。出す場面を選ぶ項目（ジャスト回避）は EvaluateGoal が決め直す
    WriteProgress(state, 0.0f, 1.0f);
    state.showHint = hasText;

    const bool cleared = EvaluateGoal(state, def, battle, events, dt);

    // ---------------------------------------------------------
    // 6. 余韻に入る・次の項目へ
    // ---------------------------------------------------------
    if (!cleared) return;

    // 補助スローは達成した時点で解く。余韻の間ほぼ止まったままにならないように
    EndAssist();

    // 余韻のない項目（明転待ち・ジャスト回避）はすぐ進める
    if (def.holdTime <= 0.0f)
    {
        Advance(state);
        return;
    }

    m_clearTimer   = def.holdTime;
    state.cleared  = true;
    state.progress = 1.0f;
}



// ---------------------------------------------------------------------------
// 達成の判定
// ---------------------------------------------------------------------------

bool TutorialSystem::EvaluateGoal(TutorialState &state, const TutorialStepDef &def, const BattleContext &battle,
                                  const FrameEvents &events, float dt)
{
    const EntityID player = battle.playerEntity;

    switch (def.goal)
    {
    // シーンに入ってすぐは明転の途中なので、時間だけで進める
    case TutorialGoal::Wait:
        return m_stepTimer >= kWaitBattleTime;

    case TutorialGoal::Move:
    {
        const float meters = AccumulateMove(battle);
        WriteProgress(state, meters, kMoveMeters);
        return meters >= kMoveMeters;
    }

    case TutorialGoal::Look:
    {
        const float degrees = AccumulateLook(player);
        WriteProgress(state, degrees, kLookDegrees);
        return degrees >= kLookDegrees;
    }

    // 2回出させて、一瞬で大きく動けることと、出した後も走り続けることを体で覚えてもらう
    case TutorialGoal::Dash:
        m_dashCount += events.dashed;
        WriteProgress(state, m_dashCount, kDashCount);
        return m_dashCount >= kDashCount;

    // 1段目の項目で2段ジャンプしても達成にする（1段目は跳んだ後なので、段の数で比べる）
    case TutorialGoal::Jump:
        return events.jumpedStage >= 1;

    case TutorialGoal::DoubleJump:
        return events.jumpedStage >= 2;

    // --- 斬る ---

    case TutorialGoal::LockOn:
    {
        const auto *pc = m_world->GetComponent<PlayerComponent>(player);
        return pc && pc->lockOnTargetID != InvalidEntityID;
    }

    case TutorialGoal::Attack:
        m_hitCount += events.playerHits;
        WriteProgress(state, m_hitCount, 1);
        return m_hitCount >= 1;

    // 本線を最後の段まで出し切ったら達成。帯は今つながっている段の数で満ちていき、途切れたら空に戻る
    // 段は1段目から数える。攻撃の項目から押し続けて途中の段から入っても、そこからは数えない
    case TutorialGoal::Combo:
        m_chainStage = AdvanceMainCombo(EnteredAnimState(player), m_chainStage);
        WriteProgress(state, m_chainStage, kMainComboStages);
        return m_chainStage >= kMainComboStages;

    // 派生は本線のどの段からでも出る。押した瞬間のスティックの向きで分かれる（PlayerAttackSystem の AttackDir）
    case TutorialGoal::ComboForward:
        return TrackTwoStep(state, EnteredAnimState(player), "Atk_LeapSlash", "Atk_DiveSlash");

    case TutorialGoal::ComboBack:
        return TrackTwoStep(state, EnteredAnimState(player), "Atk_BackSlash", "Atk_LungeSlash");

    // ダッシュ攻撃は、ダッシュ中か走っている間に攻撃すると出る（PlayerAnimParamSyncSystem の DashAttackReady）
    // 続けて押すと本線の1段目に入る
    case TutorialGoal::DashAttack:
        return TrackTwoStep(state, EnteredAnimState(player), "Atk_Dash", "Atk_Combo1");

    case TutorialGoal::AirAttack:
        return TrackTwoStep(state, EnteredAnimState(player), "Air_Attack", "Air_Dive_Start");

    // --- 読んで避けて斬り込む ---

    // 形を見る前に「回避」と言っても何を避けるのか分からないので、陣形が組まれ始めてからヒントを出す
    // ヒントを出した後は、突撃が当たる直前に補助スローを掛ける
    case TutorialGoal::JustEvade:
        if (events.justEvaded) return true;
        if (IsBossAttackFormationActive()) m_sawAttackShape = true;
        state.showHint = m_sawAttackShape;
        TickEvadeAssist(state, dt);
        return false;

    // 1回当てれば達成。当てなくても時間で進める
    // （ボスに背を向けて回避した、などで届かないことがあり、そこで止めると先を教えられない）
    case TutorialGoal::Strike:
        m_hitCount += events.playerHits;
        WriteProgress(state, m_hitCount, 1);
        return m_hitCount >= 1 || m_stepTimer >= kStrikeTimeout;

    // 説明を読む時間がたったら達成。攻撃は求めない
    // この項目に入るのは Strike の余韻の後で、ラッシュはもう終わっている
    // 当てることを条件にすると、ボスへ近づき直して攻撃するまで終わらなくなる
    case TutorialGoal::Rush:
        WriteProgress(state, m_stepTimer, kRushReadTime); // 読む時間の分だけ帯が満ちていく
        return m_stepTimer >= kRushReadTime;

    // 自由練習は達成しない。遊ぶ人がポーズメニューの「タイトルへ」で抜けるまで続ける
    case TutorialGoal::FreePlay:
        return false;
    }

    return false;
}



void TutorialSystem::WriteProgress(TutorialState &state, float done, float goal) const
{
    state.progress = (goal > 0.0f) ? (std::min)(done / goal, 1.0f) : 1.0f;
}



void TutorialSystem::WriteProgress(TutorialState &state, int done, int goal) const
{
    WriteProgress(state, static_cast<float>(done), static_cast<float>(goal));
}



// ---------------------------------------------------------------------------
// 項目の進行
// ---------------------------------------------------------------------------

void TutorialSystem::Advance(TutorialState &state)
{
    JumpToStep(state, static_cast<uint8_t>(state.step + 1));
}



void TutorialSystem::JumpToStep(TutorialState &state, uint8_t step)
{
    state.step       = step;
    m_stepTimer      = 0.0f;
    m_clearTimer     = 0.0f;
    m_sawAttackShape = false;

    // 動いた量・回した量・回数は項目ごとに 0 から数える
    m_moveDistance = 0.0f;
    m_lookAngle    = 0.0f;
    m_dashCount    = 0;
    m_hitCount     = 0;
    m_chainStage   = 0;

    // 前のフレームの値も捨てる。項目に入った直後に、前の項目の頃の値との差を見て達成にしないため
    m_hasLastSample = false;

    EndAssist();

    // 速度は連続したフレームの差で求める。間が空いた古い位置は使わない
    m_droneMotion.clear();
}



void TutorialSystem::Restart(TutorialState &state)
{
    // 項目を最初に戻すだけで、BossPhaseSystem がボスを TutorialWait にする（最初の項目はボスを待たせるため）
    JumpToStep(state, 0);
    m_assistCount = 0;
}



// ---------------------------------------------------------------------------
// 動いた量・回した量・出た攻撃
// ---------------------------------------------------------------------------

float TutorialSystem::AccumulateMove(const BattleContext &battle)
{
    // 高さは数えない。ジャンプで上下しただけで、移動したことにしないため
    const Vector3 pos(battle.playerPos);
    if (m_hasLastSample)
    {
        Vector3 delta = pos - m_lastPlayerPos;
        delta.y       = 0.0f;
        m_moveDistance += delta.Length();
    }
    m_lastPlayerPos = pos;
    m_hasLastSample = true;
    return m_moveDistance;
}



float TutorialSystem::AccumulateLook(EntityID playerID)
{
    // このプレイヤーを映している TPS カメラを探す（PlayerLockOnSystem と同じ探し方）
    const CameraBodyTPSComponent *camera = nullptr;
    m_world->ViewEach<CameraBodyTPSComponent>([&](EntityID camID) {
        const auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
        if (tps && tps->target.id == playerID) camera = tps;
    });
    if (!camera) return m_lookAngle;

    // 前のフレームからの差を、左右どちらに回したかに関係なく足していく
    if (m_hasLastSample)
    {
        float delta = camera->currentYaw - m_lastCameraYaw;

        // 差は -180〜180 度に直す。角度が 360 度を越えて回り込んだ瞬間に、大きく回したと数えないため
        while (delta > 180.0f) delta -= 360.0f;
        while (delta < -180.0f) delta += 360.0f;
        m_lookAngle += std::abs(delta);
    }
    m_lastCameraYaw = camera->currentYaw;
    m_hasLastSample = true;
    return m_lookAngle;
}



std::string TutorialSystem::EnteredAnimState(EntityID playerID)
{
    const auto *fsm = m_world->GetComponent<AnimStateMachineComponent>(playerID);
    if (!fsm) return {};

    // ステートが切り替わった瞬間だけを見る。今そのステートにいるかで見ると、
    // 前の項目の間に入ったステートのまま次の項目へ移ったとき（攻撃を当てたのがダッシュ攻撃だった、など）、
    // 何もしていないのに達成してしまう
    const uint32_t current = fsm->currentStateHash;
    const bool     changed = m_hasLastSample && current != m_lastAnimState;
    m_lastAnimState        = current;
    m_hasLastSample        = true;
    if (!changed) return {};

    // 名前で返す。呼ぶ側はアニメグラフ上の名前で比べるので、ハッシュの作り方に頼らない
    const AnimState *state = FindStateRecursive(fsm->internalGraph, current);
    return state ? state->sequenceName : std::string{};
}



int TutorialSystem::AdvanceMainCombo(const std::string &entered, int stage) const
{
    if (entered.empty()) return stage; // 切り替わっていない

    if (entered == kMainCombo[0]) return 1;
    if (stage > 0 && stage < kMainComboStages && entered == kMainCombo[stage]) return stage + 1;
    return 0;
}



bool TutorialSystem::TrackTwoStep(TutorialState &state, const std::string &entered, const char *first, const char *second)
{
    if (!entered.empty())
    {
        if (entered == first) m_chainStage = 1;
        else if (m_chainStage == 1 && entered == second) m_chainStage = 2;
        else m_chainStage = 0;
    }

    WriteProgress(state, m_chainStage, 2);
    return m_chainStage >= 2;
}



// ---------------------------------------------------------------------------
// ジャスト回避の項目（ヒントの場面と補助スロー）
// ---------------------------------------------------------------------------

bool TutorialSystem::IsBossAttackFormationActive() const
{
    const BattleContext &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasBoss()) return false;

    const auto *cmd = m_world->GetComponent<BossCommandComponent>(battle.bossEntity);
    if (!cmd) return false;
    if (cmd->droneAttackActive) return true;

    // 待機の陣形（円陣）と「命令なし」以外は、どれも撃つために組む形
    switch (cmd->requestFormation)
    {
    case DroneFormationType::None:
    case DroneFormationType::LowOrbit:
    case DroneFormationType::HighOrbit:
        return false;
    default:
        return true;
    }
}



void TutorialSystem::TickEvadeAssist(TutorialState &state, float realDt)
{
    // 処理フロー:
    // 1. 突撃中のドローンの動きを記録し、当たるまでの残りを求める
    // 2. 補助を始めるかを決める
    // 3. 補助を終えるかを決める
    // 4. スローの強さと「今だ！」の表示を決める

    const BattleContext &battle = m_world->GetResource<BattleContext>();

    // 自分で避けようとしているか。押した後はダッシュが終わるまでスローを保ち、終わったら解く
    const auto *dash    = m_world->GetComponent<PlayerDashComponent>(battle.playerEntity);
    const bool  dashing = dash && dash->isDashing;

    // ---------------------------------------------------------
    // 1. ドローンの動き
    // ---------------------------------------------------------

    // 速度は前のフレームとの差で求めるので、補助中かどうかに関係なく毎フレーム記録する
    SampleDroneMotion();
    const float timeToStrike = TimeToStrike(); // 負なら当たる機体がない

    // ---------------------------------------------------------
    // 2. 補助を始めるか
    // ---------------------------------------------------------

    // 当たるまでの残りが kAssistWarnTime 以下になったら始める
    // 直前（「今だ」と同じ 0.07 秒）で始めると、スローが効く前に当たってしまう
    // スローは次のフレームから効き、しかも 0.07 秒は等速なら4フレームしかないため
    if (!m_assistRunning)
    {
        if (dashing) return; // 自分で避けようとしている。邪魔をしない
        if (timeToStrike < 0.0f || timeToStrike > kAssistWarnTime) return;

        ++m_assistCount;
        m_assistRunning = true;
        m_assistTimer   = kAssistMaxTime;
        m_assistDashed  = false;
    }

    // ---------------------------------------------------------
    // 3. 補助を終えるか
    // ---------------------------------------------------------
    m_assistTimer -= realDt;
    if (dashing) m_assistDashed = true;

    // ダッシュを始めたら、そのダッシュが終わるまでは続ける
    // 途中で等速に戻すと、受付時間の間にドローンが届かずジャスト回避が成立しない
    const bool dashOver = m_assistDashed && !dashing;

    // 押さずに当たった・外れたら、もう助ける相手がいない
    const bool threatGone = !m_assistDashed && timeToStrike < 0.0f;

    if (m_assistTimer <= 0.0f || dashOver || threatGone)
    {
        EndAssist();
        return;
    }

    // ---------------------------------------------------------
    // 4. スローの強さと表示
    // ---------------------------------------------------------

    // 「今だ」は、当たるまでの残りがジャスト回避の受付（0.08 秒）より短い kAssistNowTime になってから入る
    // 受付より短い区間なので、この間にいつ押しても成立する。一度入ったら、押したダッシュが終わるまで保つ
    if (!m_assistDashed && timeToStrike >= 0.0f && timeToStrike <= kAssistNowTime) m_assistNow = true;

    // 予告の間に早く押した場合は予告の速さのままにする（ほぼ止めると、早押しのダッシュが数秒かかる）
    const float scale = m_assistNow ? kAssistNowScale : kAssistWarnScale;

    // イーズ時間は十分長く渡す。SustainWorldSlow は毎フレーム要求し直すと、
    // 1フレームぶんイーズで等速側へ戻った値で効くため、短いと指定より速くなる
    // 長く渡しても尾を引かない（補助を終えるときは EndAssist ですぐ等速に戻す）
    Time::SustainWorldSlow(*m_world, scale, 1000.0f);

    // 押したら「今だ！」は消す
    state.assistActive = m_assistNow && !m_assistDashed;
}



void TutorialSystem::EndAssist()
{
    // 補助を掛けていないときは触らない。ここは項目の切り替えやムービー中にも呼ばれ、
    // そのときに全体スローを消すと、撃破スローなど他の演出のスローまで止めてしまう
    //
    // 掛けていたときは、要求をやめるだけでなくすぐに消す。要求をやめるだけだと、スローがイーズで長く尾を引く
    if (m_assistRunning) Time::ClearWorldSlow(*m_world);
    m_assistRunning = false;
    m_assistNow     = false;
    m_assistTimer   = 0.0f;
    m_assistDashed  = false;
}



void TutorialSystem::SampleDroneMotion()
{
    // 突撃をやめた機体（と消えた機体）の記録を消すため、このフレームに見た機体だけで作り直す
    std::unordered_map<EntityID, DroneMotion> next;

    m_world->ViewEach<DroneComponent, TransformComponent>([&](EntityID id) {
        // ジャスト回避の対象になる攻撃状態だけを見る
        const auto *drone = m_world->GetComponent<DroneComponent>(id);
        if (drone->currentState != DroneState::FireCharge) return;

        // 速度は前のフレームとの差で求めるので、毎フレーム記録する
        const Vector3 pos(m_world->GetComponent<TransformComponent>(id)->GetWorldPosition());
        DroneMotion   motion;
        motion.position = pos;

        // --- 前のフレームとの差から速度を求める ---

        // 割るのは、このフレームにその機体が実際に進んだ時間（スロー・ヒットストップ込み）
        // 実時間で割ると、補助スローの最中は速度がほぼ 0 に見え、「当たる直前」と判定できなくなる
        auto it = m_droneMotion.find(id);
        if (it != m_droneMotion.end())
        {
            const auto *time    = m_world->GetComponent<TimeStateComponent>(id);
            const float localDt = time ? time->localDt : 0.0f;
            if (localDt > 1.0e-5f)
            {
                motion.velocity    = (pos - it->second.position) / localDt;
                motion.hasVelocity = true;
            }
            else
            {
                // ヒットストップ中は動いていないので、前の速度をそのまま使う
                motion.velocity    = it->second.velocity;
                motion.hasVelocity = it->second.hasVelocity;
            }
        }
        next.emplace(id, motion);
    });

    m_droneMotion.swap(next);
}



float TutorialSystem::TimeToStrike() const
{
    const BattleContext &battle = m_world->GetResource<BattleContext>();
    const Vector3        center(battle.playerCenter); // 足元ではなく体の中心。突撃は胸の高さに来る

    // ドローンがこの距離以下に一番近づいたものがあれば当たるとみなす
    const float missSq  = kAssistMissRadius * kAssistMissRadius;
    float       nearest = -1.0f;

    // ドローンの位置と速度から、プレイヤーに最も近づく時を求める
    for (const auto &entry : m_droneMotion)
    {
        const DroneMotion &motion = entry.second;
        if (!motion.hasVelocity) continue;

        const Vector3 rel     = motion.position - center;
        const Vector3 v       = motion.velocity;
        const float   speedSq = v.LengthSquared();
        if (speedSq < 1.0e-4f) continue;

        // 今の速度のまま進んだとき、プレイヤーへ最も近づく時刻（ゲーム時間の秒）
        const float t = -rel.Dot(v) / speedSq;
        if (t < 0.0f) continue; // 遠ざかっている

        if ((rel + v * t).LengthSquared() > missSq) continue; // 当たらずに横を通る
        if (nearest < 0.0f || t < nearest) nearest = t;
    }
    return nearest;
}



// ---------------------------------------------------------------------------
// デバッグ表示
// ---------------------------------------------------------------------------

void TutorialSystem::OnGui()
{
    if (!m_world->HasResource<TutorialState>())
    {
        ImGui::TextDisabled("TutorialState がありません");
        return;
    }
    TutorialState &state = m_world->GetResource<TutorialState>();

    // --- 今の状態 ---
    const TutorialStepDef *def = state.Current();
    ImGui::Text("項目     : %d / %d  %s", state.step + 1, kTutorialStepCount, def ? def->name : "Done");
    ImGui::Text("経過     : %.1f 秒", m_stepTimer);
    ImGui::Text("進み具合 : 移動 %.1f m / 視点 %.0f 度 / ダッシュ %d 回 / 命中 %d 回 / 連携 %d 手",
                m_moveDistance, m_lookAngle, m_dashCount, m_hitCount, m_chainStage);
    ImGui::Text("補助     : %d 回%s", m_assistCount,
                m_assistRunning ? (m_assistNow ? "（今だ）" : "（予告）") : "");

    if (m_world->HasResource<BattleContext>())
    {
        const BattleContext &battle = m_world->GetResource<BattleContext>();
        const auto *phase = battle.HasBoss() ? m_world->GetComponent<BossPhaseComponent>(battle.bossEntity) : nullptr;
        if (phase) ImGui::Text("ボス形態 : %s", ToString(phase->phase));
    }

    // --- 操作 ---

    // ボタンは状態を書き換えるだけにする。項目の判定と UI の更新は、次の Update と各 System に任せる
    ImGui::Separator();
    if (ImGui::Button("最初から開始")) Restart(state);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.IsFinished());
    if (ImGui::Button("次の項目へ")) Advance(state);
    ImGui::SameLine();
    if (ImGui::Button("完了扱いで終了")) JumpToStep(state, kTutorialStepCount); // 完了の画面が出る
    ImGui::EndDisabled();
}



// 全体登録（REGISTER_*）はせず、TutorialScene が登録する。ゲーム本編ではチュートリアルを行わないため
