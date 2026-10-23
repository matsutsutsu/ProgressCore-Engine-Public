#include "Game/Logics/Tutorial/TutorialSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/App/GameFlowState.h" // ムービー中・決着後は教えない
#include "Engine/Time/TimeService.h"       // 回避の補助スロー
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"                       // ドローンが実際に進んだ時間（速度を求めるため）
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h" // 視点の項目で、カメラの水平角を見る
#include "Engine/Animation/Core/AnimStateMachine.h"                 // コンボ・ダッシュ攻撃の項目で、今のステートを見る

#include "Game/Core/GameEvents.h"                                       // ジャスト回避・命中・ダッシュ・ジャンプのイベント
#include "Game/Logics/AI/Battle/BattleContext.h"                        // プレイヤーとボスの EntityID
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"             // 突撃中のドローンを探す
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h" // ボスが攻撃の陣形を組んでいるか
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"          // ロックオンの項目で、ロックオン先を見る

#include <SimpleMath.h>
#include <cmath> // std::abs

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h" // デバッグ表示でボスの形態を出す
#endif

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

namespace
{
    // --- 項目の達成に必要な量 ---
    constexpr float kWaitBattleTime = 1.0f;  // 明転を待つ時間（秒・実時間）
    constexpr float kMoveMeters     = 10.0f; // 移動: 水平に動く距離（m）
    constexpr float kLookDegrees    = 90.0f; // 視点: カメラを回す角度（度）
    constexpr int   kDashCount      = 2;     // ダッシュ: 出す回数
    constexpr float kStrikeTimeout  = 5.0f;  // 斬り込み: 当てなくても次へ進む時間（秒・実時間）
    constexpr float kRushReadTime   = 3.0f;  // ラッシュ: 説明を読ませる時間。当ててもこれがたつまで進まない（秒・実時間）

    // --- 回避の補助スロー ---
    constexpr int   kMaxAssists       = 3;     // 補助スローを掛ける回数の上限
    constexpr float kAssistWarnTime   = 0.5f;  // 予告のスローを始める、当たるまでの残り（秒・ゲーム時間）
    constexpr float kAssistNowTime    = 0.07f; // 「今だ」に入る、当たるまでの残り（秒・ゲーム時間）
    constexpr float kAssistMissRadius = 2.5f;  // 最も近づいたときにこの距離以下なら「当たる」とみなす（m）
    constexpr float kAssistWarnScale  = 0.3f;  // 予告中の世界のタイムスケール
    constexpr float kAssistNowScale   = 0.02f; // 「今だ」の間の世界のタイムスケール
    constexpr float kAssistEaseTime   = 1.0f;  // SustainWorldSlow に渡すイーズ時間（秒）
    constexpr float kAssistMaxTime    = 3.5f;  // 補助スローを掛け続ける上限（秒・実時間）
} // namespace

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void TutorialSystem::Initialize()
{
    // この System を登録するのは TutorialScene だけ。リソースがあることが「チュートリアル中」の印になる
    if (!m_world->HasResource<TutorialState>())
        m_world->AddResource<TutorialState>();

    // イベントは発行元の System の中で届くので、印を立てるだけにして Update で読む
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &) {
        m_justEvaded = true;
    });
    ListenEvent<AttackHitConfirmedEvent>([this](const AttackHitConfirmedEvent &e) {
        if (e.attackerID == m_playerID) ++m_playerHits;
    });
    ListenEvent<PlayerDashedEvent>([this](const PlayerDashedEvent &e) {
        if (e.playerID == m_playerID) ++m_dashed;
    });

    // 跳ぶのはプレイヤーだけなので、誰が跳んだかは見ない
    ListenEvent<PlayerJumpedEvent>([this](const PlayerJumpedEvent &e) {
        m_jumpedStage = e.jumpCount;
    });
}

std::vector<TypeID> TutorialSystem::GetReadTypes() const
{
    return {
        TypeInfo<DroneComponent>::ID(),
        TypeInfo<TransformComponent>::ID(),
        TypeInfo<BossCommandComponent>::ID(),
        TypeInfo<TimeStateComponent>::ID(),
        TypeInfo<CameraBodyTPSComponent>::ID(),
        TypeInfo<PlayerComponent>::ID(),
        TypeInfo<AnimStateMachineComponent>::ID(),
    };
}

void TutorialSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 教えている途中かを確かめる
    // 2. イベントの印を取り出す
    // 3. ムービー中と決着後は教えない
    // 4. 今の項目を達成したかを判定する
    // 5. 達成したら次の項目へ進める

    // 【備考】
    //   何を見せるか（文言・順番）は TutorialSteps.h の表が持ち、
    //   何をどれだけしたら達成か（量・時間切れ）は、このファイルの先頭の定数と下の switch が持つ。

    // ---------------------------------------------------------
    // 1. 教えている途中か
    // ---------------------------------------------------------
    if (!m_world->HasResource<TutorialState>()) return;
    TutorialState &state = m_world->GetResource<TutorialState>();

    // ヒントと補助の表示は、出す場面かどうかを毎フレーム決め直す
    state.showHint     = false;
    state.assistActive = false;
    if (state.IsFinished()) return;

    if (!m_world->HasResource<BattleContext>()) return;
    const BattleContext &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return;
    m_playerID = battle.playerEntity;

    // ---------------------------------------------------------
    // 2. イベントの印
    // ---------------------------------------------------------

    // 印は毎フレーム取り出して消す。関係のない項目の間に立った印を、後の項目へ持ち越さないため
    const bool justEvaded  = m_justEvaded;
    const int  playerHits  = m_playerHits;
    const int  dashed      = m_dashed;
    const int  jumpedStage = m_jumpedStage; // このフレームに跳んだ段（跳んでいなければ 0）
    m_justEvaded  = false;
    m_playerHits  = 0;
    m_dashed      = 0;
    m_jumpedStage = 0;

    // ---------------------------------------------------------
    // 3. ムービー中と決着後
    // ---------------------------------------------------------

    // ムービー中はプレイヤーが操作できず、決着後はもう戦わない。どちらもヒントを出しても押せない
    if (m_world->HasResource<GameFlowState>())
    {
        const GameFlowState &flow = m_world->GetResource<GameFlowState>();
        if (flow.cinematic || flow.matchOver)
        {
            EndAssist();

            // 止まっている間は記録しないので、明けた直後に古い位置・角度との差を足さないよう消しておく
            m_droneMotion.clear();
            m_hasLastSample = false;
            return;
        }
    }

    // ---------------------------------------------------------
    // 4. 達成の判定
    // ---------------------------------------------------------
    m_stepTimer += dt;

    const TutorialStepDef &def     = kTutorialSteps[state.step];
    bool                   cleared = false;

    // ヒントは文言のある項目で出す。出す場面を選ぶ項目（ジャスト回避）は下で決め直す
    state.showHint = def.text[0] != '\0';

    switch (def.goal)
    {
    // シーンに入ってすぐは明転の途中なので、時間だけで進める
    case TutorialGoal::Wait:
        cleared = m_stepTimer >= kWaitBattleTime;
        break;

    case TutorialGoal::Move:
        cleared = AccumulateMove(battle) >= kMoveMeters;
        break;

    case TutorialGoal::Look:
        cleared = AccumulateLook(battle.playerEntity) >= kLookDegrees;
        break;

    // 2回出させて、一瞬で大きく動けることと、出した後も走り続けることを体で覚えてもらう
    case TutorialGoal::Dash:
        m_dashCount += dashed;
        cleared = m_dashCount >= kDashCount;
        break;

    // 1段目の項目で2段ジャンプしても達成にする（1段目は跳んだ後なので、段の数で比べる）
    case TutorialGoal::Jump:
        cleared = jumpedStage >= 1;
        break;

    case TutorialGoal::DoubleJump:
        cleared = jumpedStage >= 2;
        break;

    case TutorialGoal::LockOn:
        if (const auto *player = m_world->GetComponent<PlayerComponent>(battle.playerEntity))
            cleared = player->lockOnTargetID != InvalidEntityID;
        break;

    case TutorialGoal::Attack:
        m_hitCount += playerHits;
        cleared = m_hitCount >= 1;
        break;

    // コンボは Attack_1 → Attack_2 の2段（アニメグラフの AttackCombo）。2段目に入れば続けて押せている。
    // この項目に入ってから1段目を出したコンボだけを数える。攻撃の項目から押し続けていると、
    // 入った直後に2段目へ移って達成になり、ヒントが一瞬で消えてしまうため
    case TutorialGoal::Combo:
    {
        const std::string entered = EnteredAnimState(battle.playerEntity);
        if (entered == "Attack_1") m_comboStarted = true;
        cleared = m_comboStarted && entered == "Attack_2";
        break;
    }

    // ダッシュ攻撃は、ダッシュ中か走っている間に攻撃すると出る（PlayerAnimParamSyncSystem の DashAttackReady）
    case TutorialGoal::DashAttack:
        cleared = EnteredAnimState(battle.playerEntity) == "DashAttack";
        break;

    // 形を見る前に「回避」と言っても何を避けるのか分からないので、陣形が組まれ始めてからヒントを出す。
    // ヒントを出した後は、突撃が当たる直前に補助スローを掛ける
    case TutorialGoal::JustEvade:
        cleared = justEvaded;
        if (cleared) break;
        if (IsBossAttackFormationActive()) m_sawAttackShape = true;
        state.showHint = m_sawAttackShape;
        TickEvadeAssist(state, dt);
        break;

    // 1回当てれば達成。当てなくても時間で進める
    // （ボスに背を向けて回避した、などで届かないことがあり、そこで止めると先を教えられない）
    case TutorialGoal::Strike:
        m_hitCount += playerHits;
        cleared = m_hitCount >= 1 || m_stepTimer >= kStrikeTimeout;
        break;

    // 1回当てて、かつ説明を読む時間がたったら達成（最後の項目なので、教え終える）。
    // Strike の当たりはラッシュの途中で出るので、当てただけで進めると説明が一瞬で消えてしまう
    case TutorialGoal::Rush:
        m_hitCount += playerHits;
        cleared = m_hitCount >= 1 && m_stepTimer >= kRushReadTime;
        break;
    }

    // ---------------------------------------------------------
    // 5. 次の項目へ
    // ---------------------------------------------------------

    // 最後の項目を終えると step が kTutorialStepCount になり、教え終える
    if (cleared) Advance(state, static_cast<uint8_t>(state.step + 1));
}

// ---------------------------------------------------------------------------
// 項目の進行
// ---------------------------------------------------------------------------

void TutorialSystem::Advance(TutorialState &state, uint8_t next)
{
    state.step       = next;
    m_stepTimer      = 0.0f;
    m_sawAttackShape = false;

    // 動いた量・回した量・回数は項目ごとに 0 から数える
    m_moveDistance = 0.0f;
    m_lookAngle    = 0.0f;
    m_dashCount    = 0;
    m_hitCount     = 0;
    m_comboStarted = false;

    // 前のフレームの値も捨てる。項目に入った直後に、前の項目の頃の値との差を見て達成にしないため
    m_hasLastSample = false;

    EndAssist();

    // 速度は連続したフレームの差で求める。間が空いた古い位置は使わない
    m_droneMotion.clear();
}

void TutorialSystem::Restart(TutorialState &state)
{
    // 項目を最初に戻すだけで、BossPhaseSystem がボスを TutorialWait にする（最初の項目はボスを待たせるため）
    Advance(state, 0);
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

    // TODO(2026-10-11): PlayerDashComponent.isDashing から「自分で避けようとしているか」を読む。
    //   今は常に false なので、補助中にダッシュを押しても押したと分からず、
    //   失敗したときは「今だ！」とほぼ止めたスローが、突撃が通り過ぎるか時間切れまで続く
    const bool dashing = false;
    (void)battle;

    // ---------------------------------------------------------
    // 1. ドローンの動き
    // ---------------------------------------------------------

    // 速度は前のフレームとの差で求めるので、補助中かどうかに関係なく毎フレーム記録する
    SampleDroneMotion();
    const float timeToStrike = TimeToStrike(); // 負なら当たる機体が無い

    // ---------------------------------------------------------
    // 2. 補助を始めるか
    // ---------------------------------------------------------

    // 当たるまでの残りが kAssistWarnTime 以下になったら始める。
    // 直前（「今だ」と同じ 0.07 秒）で始めると、スローが効く前に当たってしまう。
    // スローは次のフレームから効き、しかも 0.07 秒は等速なら4フレームしかないため
    if (!m_assistRunning)
    {
        if (m_assistCount >= kMaxAssists) return;
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

    // ダッシュを始めたら、そのダッシュが終わるまでは続ける。
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

    // 「今だ」は、当たるまでの残りがジャスト回避の受付（0.08 秒）より短い kAssistNowTime になってから入る。
    // 受付より短い区間なので、この間にいつ押しても成立する。一度入ったら、押したダッシュが終わるまで保つ
    if (!m_assistDashed && timeToStrike >= 0.0f && timeToStrike <= kAssistNowTime) m_assistNow = true;

    // 予告（kAssistWarnScale）は 0.5 秒ぶんの接近を実時間で約 1.5 秒に延ばし、迫ってくるのを見せる。
    // 「今だ」（kAssistNowScale）は 0.07 秒ぶんを実時間で数秒に延ばし、落ち着いて押せるようにする。
    // 予告の間に早く押した場合は予告の速さのままにする（ほぼ止めると、早押しのダッシュが数秒かかる）
    const float scale = m_assistNow ? kAssistNowScale : kAssistWarnScale;

    // 毎フレーム要求し直すと、1フレームぶんイーズで戻った値で効く。
    // イーズ時間を長くするほど指定の倍率に近づく（kAssistEaseTime = 1 秒で、0.02 が約 0.05 になる）
    Time::SustainWorldSlow(*m_world, scale, kAssistEaseTime);

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

        // 割るのは、このフレームにその機体が実際に進んだ時間（スロー・ヒットストップ込み）。
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

#ifdef ENABLE_EDITOR
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
    ImGui::Text("進み具合 : 移動 %.1f m / 視点 %.0f 度 / ダッシュ %d 回 / 命中 %d 回",
                m_moveDistance, m_lookAngle, m_dashCount, m_hitCount);
    ImGui::Text("補助     : %d / %d 回%s", m_assistCount, kMaxAssists,
                m_assistRunning ? (m_assistNow ? "（今だ）" : "（予告）") : "");

    if (m_world->HasResource<BattleContext>())
    {
        const BattleContext &battle = m_world->GetResource<BattleContext>();
        if (battle.HasBoss())
        {
            if (const auto *phase = m_world->GetComponent<BossPhaseComponent>(battle.bossEntity))
                ImGui::Text("ボス形態 : %s", ToString(phase->phase));
        }
    }

    // --- 操作 ---

    // ボタンは状態を書き換えるだけにする。項目の判定と UI の更新は、次の Update と各 System に任せる
    ImGui::Separator();
    if (ImGui::Button("最初から開始")) Restart(state);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.IsFinished());
    if (ImGui::Button("次の項目へ")) Advance(state, static_cast<uint8_t>(state.step + 1));
    ImGui::SameLine();
    if (ImGui::Button("完了扱いで終了")) Advance(state, kTutorialStepCount); // 完了の画面が出る
    ImGui::EndDisabled();
}
#else
void TutorialSystem::OnGui() {}
#endif

// 全体登録（REGISTER_*）はせず、TutorialScene が登録する。ゲーム本編ではチュートリアルを行わないため
