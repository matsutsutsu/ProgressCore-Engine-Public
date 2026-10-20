#include "Game/Logics/Tutorial/TutorialSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/App/GameFlowState.h" // ムービー中・決着後は教えない
#include "Engine/Time/TimeService.h"       // 回避の補助スロー
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h" // ドローンが実際に進んだ時間（速度を求めるため）
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h" // 視点の項目で、カメラの水平角を見る
#include "Engine/Animation/Core/AnimStateMachine.h"                 // コンボ・ダッシュ攻撃の項目で、今のステートを見る

#include "Game/Logics/Character/Player/Core/PlayerComponent.h" // ロックオンの項目で、ロックオン先を見る

#include "Game/Core/GameEvents.h"                                         // ジャスト回避・命中のイベント
#include "Game/Logics/AI/Battle/BattleContext.h"                          // プレイヤーとボスの EntityID
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"               // 突撃中のドローンを探す
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"   // ボスが攻撃の陣形を組んでいるか

#include <SimpleMath.h>
#include <algorithm> // std::max
#include <cmath>     // std::abs

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h" // デバッグ表示でボスの形態を出す
#endif

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

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
        m_justEvaded.store(true);
    });
    ListenEvent<AttackHitConfirmedEvent>([this](const AttackHitConfirmedEvent &e) {
        if (e.attackerID == m_playerID.load()) m_playerHits.fetch_add(1);
    });
    ListenEvent<PlayerDashedEvent>([this](const PlayerDashedEvent &e) {
        if (e.playerID == m_playerID.load()) m_dashed.fetch_add(1);
    });
    // 跳ぶのはプレイヤーだけなので、誰が跳んだかは見ない
    ListenEvent<PlayerJumpedEvent>([this](const PlayerJumpedEvent &e) {
        m_jumpedStage.store(e.jumpCount);
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
    if (!m_world->HasResource<TutorialState>()) return;
    TutorialState &state = m_world->GetResource<TutorialState>();

    // ヒントと補助の表示は、出す場面かどうかを毎フレーム決め直す
    state.showHint     = false;
    state.assistActive = false;
    if (state.IsFinished()) return;

    if (!m_world->HasResource<BattleContext>()) return;
    const BattleContext &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return;
    m_playerID.store(battle.playerEntity);

    // 印は毎フレーム取り出して消す。関係のない項目の間に立った印を、後の項目へ持ち越さないため
    const bool justEvaded  = m_justEvaded.exchange(false);
    const int  playerHits  = m_playerHits.exchange(0);
    const int  dashed      = m_dashed.exchange(0);
    const int  jumpedStage = m_jumpedStage.exchange(0); // このフレームに跳んだ段（跳んでいなければ 0）

    // --- ムービー中と決着後は教えない ---
    // ムービー中はプレイヤーが操作できず、決着後はもう戦わない。どちらもヒントを出しても押せない
    if (m_world->HasResource<GameFlowState>())
    {
        const GameFlowState &flow = m_world->GetResource<GameFlowState>();
        if (flow.cinematic || flow.matchOver)
        {
            EndAssist();
            m_droneMotion.clear(); // ムービーの間は記録しないので、明けた直後に古い位置との差を速度にしない
            m_hasLastSample = false; // 移動・視点も同じく、明けた直後に古い位置・角度との差を足さない
            return;
        }
    }

    m_stepTimer += dt;

    // --- 達成したかを、項目の条件の種類ごとに判定する ---
    // 何を教えるか（文言・順番・時間切れ）は TutorialSteps.h の表が持ち、ここは判定だけを持つ
    const TutorialStepDef &def     = kTutorialSteps[state.step];
    bool                   cleared = false;

    // ヒントは文言のある項目で出す。出す場面を選ぶ項目（ジャスト回避）は下で決め直す
    state.showHint = def.text[0] != '\0';

    switch (def.goal)
    {
    // 時間切れだけで進む
    case TutorialGoal::Wait:
        break;

    case TutorialGoal::Move:
        cleared = AccumulateMove(battle) >= def.amount;
        break;

    case TutorialGoal::Look:
        cleared = AccumulateLook(battle.playerEntity) >= def.amount;
        break;

    case TutorialGoal::Dash:
        m_dashCount += dashed;
        cleared = static_cast<float>(m_dashCount) >= def.amount;
        break;

    // 1段目の項目で2段ジャンプしても達成にする（1段目は跳んだ後なので、段の数で比べる）
    case TutorialGoal::Jump:
        cleared = static_cast<float>(jumpedStage) >= def.amount;
        break;

    case TutorialGoal::LockOn:
        if (const auto *player = m_world->GetComponent<PlayerComponent>(battle.playerEntity))
            cleared = player->lockOnTargetID != InvalidEntityID;
        break;

    // amount が 0 の項目（章3の攻撃）は、1回当てれば達成にする
    case TutorialGoal::AttackHit:
        m_hitCount += playerHits;
        cleared = static_cast<float>(m_hitCount) >= std::max(def.amount, 1.0f);
        break;

    case TutorialGoal::AnimState:
        cleared = EnteredAnimState(battle.playerEntity, def.animState);
        break;

    // 陣形が組まれ始めてからヒントを出し、突撃が当たる直前に補助スローを掛ける
    case TutorialGoal::JustEvade:
        cleared = justEvaded;
        if (cleared) break;
        if (IsBossAttackFormationActive()) m_sawAttackShape = true;
        state.showHint = m_sawAttackShape;
        TickEvadeAssist(state, dt);
        break;
    }

    // 達成したか時間切れなら次へ。最後の項目を終えると step が kTutorialStepCount になり、教え終える
    const bool timedOut = def.timeout > 0.0f && m_stepTimer >= def.timeout;
    if (cleared || timedOut) Advance(state, static_cast<uint8_t>(state.step + 1));
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
    m_moveDistance  = 0.0f;
    m_lookAngle     = 0.0f;
    m_dashCount     = 0;
    m_hitCount      = 0;
    m_hasLastSample = false;

    EndAssist();
    m_droneMotion.clear(); // 速度は連続したフレームの差で求める。間が空いた古い位置は使わない
}

void TutorialSystem::Restart(TutorialState &state)
{
    // 項目を Done から戻すだけで、BossPhaseSystem がボスを序盤に入れ直す（教えている途中なら序盤へ入るため）
    Advance(state, 0);
    m_assistCount = 0;
}


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

// ---------------------------------------------------------------------------
// 動いた量・回した量
// ---------------------------------------------------------------------------

float TutorialSystem::AccumulateMove(const BattleContext &battle)
{
    // 足元の位置の、前のフレームからの水平の差を足していく。
    // 高さは数えない（ジャンプで上下しただけで移動したことにしない）
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

    // 前のフレームからの水平角の差を、向きに関係なく足していく。
    // 差は -180〜180 度に直す。角度が 360 度を越えて回り込んだ瞬間に、大きく回したと数えないため
    if (m_hasLastSample)
    {
        float delta = camera->currentYaw - m_lastCameraYaw;
        while (delta > 180.0f) delta -= 360.0f;
        while (delta < -180.0f) delta += 360.0f;
        m_lookAngle += std::abs(delta);
    }
    m_lastCameraYaw = camera->currentYaw;
    m_hasLastSample = true;
    return m_lookAngle;
}

bool TutorialSystem::EnteredAnimState(EntityID playerID, const char *stateName)
{
    const auto *fsm = m_world->GetComponent<AnimStateMachineComponent>(playerID);
    if (!fsm || !stateName) return false;

    // ステートが切り替わった瞬間だけを見る。今そのステートにいるかで見ると、
    // 前の項目の間に入ったステートのまま次の項目へ移ったとき（攻撃の3回目がコンボの2段目だった、など）、
    // 何もしていないのに達成してしまう
    const uint32_t current = fsm->currentStateHash;
    const bool     changed = m_hasLastSample && current != m_lastAnimState;
    m_lastAnimState        = current;
    m_hasLastSample        = true;
    if (!changed) return false;

    // ステートは名前で比べる。表に書くのはアニメグラフ上の名前なので、ハッシュの作り方に頼らない
    const AnimState *state = FindStateRecursive(fsm->internalGraph, current);
    return state && state->sequenceName == stateName;
}

// ---------------------------------------------------------------------------
// 回避の補助スロー
// ---------------------------------------------------------------------------

void TutorialSystem::TickEvadeAssist(TutorialState &state, float realDt)
{
    const BattleContext &battle = m_world->GetResource<BattleContext>();
    // 回避ダッシュは作り直し中。復活したら PlayerDashComponent から「自分で避けようとしているか」を戻す。
    const bool dashing = false;
    (void)battle;

    // 速度は前のフレームとの差で求めるので、補助中かどうかに関係なく毎フレーム記録する
    SampleDroneMotion();
    const float timeToStrike = TimeToStrike(); // 負なら当たる機体が無い

    // --- 補助を始めるか ---
    // 当たるまでの残りが kAssistWarnTime 以下になったら始める。
    // 直前（ジャスト回避の受付と同じ 0.07 秒）で始めると、スローが効く前に当たってしまう。
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

    // --- 補助中: 押すまで、または押したダッシュが終わるまでスローを保つ ---
    m_assistTimer -= realDt;
    if (dashing) m_assistDashed = true;

    // ダッシュを始めたら、そのダッシュが終わるまでは続ける。途中で等速に戻すと、
    // 受付時間の間にドローンが届かずジャスト回避が成立しない
    const bool dashOver   = m_assistDashed && !dashing;
    const bool threatGone = !m_assistDashed && timeToStrike < 0.0f; // 押さずに当たった・外れた
    if (m_assistTimer <= 0.0f || dashOver || threatGone)
    {
        EndAssist();
        return;
    }

    // 「今だ」はジャスト回避の受付より短い残りになってから。一度入ったら、押したダッシュが終わるまで保つ。
    // 予告の間に早く押した場合は予告の速さのままにする（ほぼ止めると、早押しのダッシュが数秒かかる）
    if (!m_assistDashed && timeToStrike >= 0.0f && timeToStrike <= kAssistNowTime) m_assistNow = true;
    Time::SustainWorldSlow(*m_world, m_assistNow ? kAssistNowScale : kAssistWarnScale, kAssistEaseTime);
    state.assistActive = m_assistNow && !m_assistDashed; // 押したら「今だ！」は消す
}

void TutorialSystem::EndAssist()
{
    // 補助を掛けていないときは触らない。ここは項目の切り替えやムービー中にも呼ばれ、
    // そのときに全体スローを消すと、撃破スローなど他の演出のスローまで止めてしまう
    if (m_assistRunning) Time::ClearWorldSlow(*m_world);
    m_assistRunning = false;
    m_assistNow     = false;
    m_assistTimer   = 0.0f;
    m_assistDashed  = false;
}

void TutorialSystem::SampleDroneMotion()
{
    // 突撃をやめた機体（と消えた機体）の記録を消すため、このフレームに見た機体へ印を付ける
    std::unordered_map<EntityID, DroneMotion> next;

    m_world->ViewEach<DroneComponent, TransformComponent>([&](EntityID id) {
        // ジャスト回避の対象になる状態だけを見る（PlayerJustEvadeSystem::IsDroneHostile と同じ条件）
        const auto *drone = m_world->GetComponent<DroneComponent>(id);
        if (drone->currentState != DroneState::FireCharge) return;

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

    const float missSq  = kAssistMissRadius * kAssistMissRadius;
    float       nearest = -1.0f;

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
    ImGui::Text("章       : %s", def ? kTutorialChapterNames[static_cast<size_t>(def->chapter)] : "-");
    ImGui::Text("項目     : %d / %d  %s", state.step + 1, kTutorialStepCount, def ? def->name : "Done");
    ImGui::Text("経過     : %.1f 秒", m_stepTimer);
    if (def && def->amount > 0.0f)
        ImGui::Text("進み具合 : 移動 %.1f m / 視点 %.0f 度 / ダッシュ %d 回 / 命中 %d 回（必要な量 %.1f）",
                    m_moveDistance, m_lookAngle, m_dashCount, m_hitCount, def->amount);
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
    // ボタンはロジックの Update と同じフレームに混ざらないよう、状態を書き換えるだけにする
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
