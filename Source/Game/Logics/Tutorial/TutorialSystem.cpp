#include "Game/Logics/Tutorial/TutorialSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/App/GameFlowState.h" // ムービー中・決着後は教えない
#include "Engine/Time/TimeService.h"       // 回避の補助スロー
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h" // ドローンが実際に進んだ時間（速度を求めるため）

#include "Game/Core/GameEvents.h"                                         // ジャスト回避・命中のイベント
#include "Game/Logics/AI/Battle/BattleContext.h"                          // プレイヤーとボスの EntityID
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"               // 突撃中のドローンを探す
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"   // ボスが攻撃の陣形を組んでいるか

#include <SimpleMath.h>
#include <iterator> // std::size

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
    // タイトル画面も全 System を登録するので、ここでも作られる。プレイヤーが居なければ Update が何もしない
    if (!m_world->HasResource<TutorialState>())
        m_world->AddResource<TutorialState>();

    // タイトルを通った挑戦で、まだ教え終えていないときだけ行う。
    // エディタシーンから直接遊ぶとき（armed が立たない）と、教え終えた後のリトライでは出さない
    const TutorialSession &session = TutorialSession::Instance();
    TutorialState         &state   = m_world->GetResource<TutorialState>();
    state                          = TutorialState{};
    state.enabled                  = session.armed && !session.completed;
    if (state.enabled) state.generation = 1;

    // イベントは発行元の System の中で届くので、印を立てるだけにして Update で読む
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &) {
        m_justEvaded.store(true);
    });
    ListenEvent<AttackHitConfirmedEvent>([this](const AttackHitConfirmedEvent &e) {
        if (e.attackerID == m_playerID.load()) m_playerHit.store(true);
    });
}

std::vector<TypeID> TutorialSystem::GetReadTypes() const
{
    return {
        TypeInfo<DroneComponent>::ID(),
        TypeInfo<TransformComponent>::ID(),
        TypeInfo<BossCommandComponent>::ID(),
        TypeInfo<TimeStateComponent>::ID(),
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
    const bool justEvaded = m_justEvaded.exchange(false);
    const bool playerHit  = m_playerHit.exchange(false);

    // --- ムービー中と決着後は教えない ---
    // ムービー中はプレイヤーが操作できず、決着後はもう戦わない。どちらもヒントを出しても押せない
    if (m_world->HasResource<GameFlowState>())
    {
        const GameFlowState &flow = m_world->GetResource<GameFlowState>();
        if (flow.cinematic || flow.matchOver)
        {
            EndAssist();
            m_droneMotion.clear(); // ムービーの間は記録しないので、明けた直後に古い位置との差を速度にしない
            return;
        }
    }

    m_stepTimer += dt;

    switch (state.step)
    {
    // 開幕ムービーが明けてすぐはカメラが落ち着いていないので、少し待ってから始める
    case TutorialStep::WaitBattle:
        if (m_stepTimer >= kStepGap) Advance(state, TutorialStep::ReadAndEvade);
        break;

    // 回避: 陣形が組まれ始めてからヒントを出す。形を見る前に「回避」と言っても、何を避けるのか分からない
    case TutorialStep::ReadAndEvade:
        if (justEvaded)
        {
            Advance(state, TutorialStep::Strike);
            break;
        }
        if (IsBossAttackFormationActive()) m_sawAttackShape = true;
        state.showHint = m_sawAttackShape;
        TickEvadeAssist(state, dt);
        break;

    // 攻撃: ジャスト回避の接近でボスの近くへ運ばれている間に促す。当てなくても時間で次へ進める
    // （ボスに背を向けて回避した、などで届かないことがあり、そこで止めると先を教えられない）。
    // これが最後の項目。当てるか時間切れで教え終える。
    case TutorialStep::Strike:
        state.showHint = true;
        if (playerHit || m_stepTimer >= kStrikeTimeout)
        {
            Advance(state, TutorialStep::Done);
            TutorialSession::Instance().completed = true;
        }
        break;

    case TutorialStep::Done:
        break;
    }
}

// ---------------------------------------------------------------------------
// 項目の進行
// ---------------------------------------------------------------------------

void TutorialSystem::Advance(TutorialState &state, TutorialStep next)
{
    state.step       = next;
    m_stepTimer      = 0.0f;
    m_sawAttackShape = false;
    m_sawLunge       = false;

    EndAssist();
    m_droneMotion.clear(); // 速度は連続したフレームの差で求める。間が空いた古い位置は使わない
}

void TutorialSystem::Restart(TutorialState &state)
{
    state.enabled = true;
    ++state.generation; // BossPhaseSystem がこの変化を見て、ボスを序盤に入れ直す
    Advance(state, TutorialStep::WaitBattle);
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
    const size_t stepIndex = static_cast<size_t>(state.step);
    ImGui::Text("有効     : %s", state.enabled ? "はい" : "いいえ（タイトルを通っていない／教え終えた）");
    ImGui::Text("項目     : %s", stepIndex < std::size(kTutorialStepNames) ? kTutorialStepNames[stepIndex] : "?");
    ImGui::Text("経過     : %.1f 秒", m_stepTimer);
    ImGui::Text("補助     : %d / %d 回%s", m_assistCount, kMaxAssists,
                m_assistRunning ? (m_assistNow ? "（今だ）" : "（予告）") : "");

    if (m_world->HasResource<BattleContext>())
    {
        const BattleContext &battle = m_world->GetResource<BattleContext>();
        if (battle.HasBoss())
        {
            if (const auto *phase = m_world->GetComponent<BossPhaseComponent>(battle.bossEntity))
                ImGui::Text("ボス形態 : %s（序盤の残り %.1f 秒）", ToString(phase->phase), phase->timer);
        }
    }

    // --- 操作 ---
    // ボタンはロジックの Update と同じフレームに混ざらないよう、状態を書き換えるだけにする
    ImGui::Separator();
    if (ImGui::Button("最初から開始")) Restart(state);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.IsFinished());
    if (ImGui::Button("次の項目へ"))
        Advance(state, static_cast<TutorialStep>(static_cast<uint8_t>(state.step) + 1));
    ImGui::SameLine();
    if (ImGui::Button("完了扱いで終了")) Advance(state, TutorialStep::Done); // ボスは本来の BT へ戻る
    ImGui::EndDisabled();

    ImGui::TextDisabled("エディタシーンでは自動で始まりません。確かめるときは「最初から開始」");
}
#else
void TutorialSystem::OnGui() {}
#endif

// HP と切り払いの状態が確定した後に見る。ヒントを出す TutorialHintHudSystem も同じステージで、この後に走る
REGISTER_LOGIC_SYSTEM(TutorialSystem, Priority::LogicStage::L06_HudSync);
