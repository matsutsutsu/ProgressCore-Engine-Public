#include "BossPhaseSystem.h"

#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Game/Logics/Tutorial/TutorialState.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Cinematic/CinematicEvents.h"
#include "Engine/Time/TimeService.h"


namespace
{
    // 第1形態の HP の下限を、しきい値よりこの幅だけ上に置く（最大 HP に対する %）
    // ちょうどだと「しきい値以下なら移行」に入り、とどめを待たずに移ってしまう
    constexpr float kLineMarginPercent = 1.0f;

    // 最後の形態の HP の下限。0 より上なので、とどめ以外では倒れない
    constexpr float kLastStandHealth = 1.0f;
} // namespace



void BossPhaseSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. ボス本体への要求を空にし、怒り中かを確定する
    // 2. 次の形態を決め、変わったフレームだけ入ったときの処理をする
    // 3. 今の形態の毎フレームの処理をする
    // 4. とどめ以外で下がらない HP の下限を書く
    // 5. 移行演出のタイマーを進める
    // 6. 移行のスローが終わったら、移行ムービーを頼む

    // TutorialState があるのはチュートリアルシーンだけ
    // なければ TutorialWait / TutorialAttack に入らない
    const TutorialState *tutorial = m_world->HasResource<TutorialState>() ? &m_world->GetResource<TutorialState>() : nullptr;
    const bool           inTutorial  = tutorial != nullptr;

    // 攻撃を覚える前の項目と教え終えた後は、攻撃を出さずに待つ
    const bool bossWaiting = tutorial && tutorial->IsBossWaiting();

    // 移行ムービーが流れている間は、移行演出（無敵・BT 停止）を続ける
    const bool cinematic = m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().cinematic;

    ForEach([&](BossPhaseComponent       &phase,
                BehaviorTreeComponent    &bt,
                BossCommandComponent     &cmd,
                AnimParametersComponent  &anim,
                HealthComponent          &health,
                const TimeStateComponent &time) {
        // ---------------------------------------------------------
        // 1. 要求を空にする
        // ---------------------------------------------------------

        // 本体への要求は、BT の技が続いている間だけ毎フレーム書き直す
        // 先に空にしておき、このフレームに書かれなければ取り下げたとみなす
        // （ドローンの陣形と攻撃信号は次の指示まで保つので触らない）
        cmd.currentActionId = 0;

        // 怒りの行動は Parallel の従なので、主の技より後に評価される
        // 同じフレームの値を読むと主の技からは毎回 false に見えるので、前のフレームの値を読ませる
        cmd.raging        = cmd.rageRequested;
        cmd.rageRequested = false;

        // ---------------------------------------------------------
        // 2〜5. 形態
        // ---------------------------------------------------------

        // 次の第２形態に遷移していいか
        const BossPhase next = DecideNextPhase(phase, health, inTutorial, bossWaiting, cinematic);

        if (next != phase.phase)
        {
            OnEnterPhase(next, phase, bt, cmd, anim, health);
            phase.phase = next;
        }

        TickPhase(phase, cmd, health);

        // 形態の線は、とどめでしか越えられないようにする
        // HP を減らす DamageResolutionSystem（L06）がこの値を見る
        health.finisherFloor = DecideFinisherFloor(phase, health);

        // 時間で終わる形態（移行演出）のためのタイマー
        if (phase.timer > 0.0f) phase.timer -= time.localDt;

        // ---------------------------------------------------------
        // 6. 移行ムービー
        // ---------------------------------------------------------

        // スローの長さは実時間なので、スローのかからない rawDt で数える
        // localDt で数えると、スローの間はほとんど減らない
        if (phase.movieDelayTimer > 0.0f)
        {
            phase.movieDelayTimer -= rawDt;

            // 流れている間は、プレイヤーの操作と敵の思考が止まる
            // 受け付けるかは CinematicCameraSystem が決める（台本がなければログを出して捨てる）
            if (phase.movieDelayTimer <= 0.0f)
                m_world->GetEventBus().Publish(CinematicPlayRequestEvent{kPhase2Cinematic});
        }
    });
}



BossPhase BossPhaseSystem::DecideNextPhase(const BossPhaseComponent &phase,
                                           const HealthComponent    &health,
                                           bool                      inTutorial,
                                           bool                      bossWaiting,
                                           bool                      cinematic) const
{
    // --- 死亡 ---

    // 死亡した際はreturn、BossPhase::Dead にする。BT は止まる
    if (phase.phase == BossPhase::Dead) return BossPhase::Dead;

    // DeadTag の付与はダメージと同フレームとは限らないので、HP を直接見る
    if (health.currentHealth <= 0.0f) return BossPhase::Dead;

    // --- 移行演出 → 第2形態 ---

    // 移行演出は、決めた時間が過ぎ、かつ移行ムービーが終わるまで続ける
    // ムービーはスローの後に頼む。timer はボスの時間で減り、スロー中はほとんど減らないので、
    // 頼む頃はまだ演出中になっている
    // 台本がなくてムービーが流れなければ、決めた時間で第2形態へ移る
    if (phase.phase == BossPhase::Transition)
    {
        const bool holding = (phase.timer > 0.0f) || cinematic;
        return holding ? BossPhase::Transition : BossPhase::Combat2;
    }

    // 第1形態で HP がしきい値を割ったら移行演出へ
    // 第2形態の BT が未設定なら移行しない（設定漏れでボスが止まるのを防ぐ）
    // TutorialWait / TutorialAttack は見なくてよい
    // チュートリアルでは TutorialHealthGuardSystem が HP をしきい値より下げない
    if (phase.phase == BossPhase::Combat && !phase.phase2AssetPath.empty() && health.maxHealth > 0.0f)
    {
        const float hpPercent = (health.currentHealth / health.maxHealth) * 100.0f;
        if (hpPercent <= phase.phase2HealthThreshold) return BossPhase::Transition;
    }

    // --- チュートリアル ---

    // チュートリアルシーンでは、最初から最後まで TutorialWait / TutorialAttack
    // だけで戦い、本来の戦い（Combat）には入らない
    // 教えている項目（bossAttacks）に合わせて行き来し、
    // 教え終えたら TutorialWait で止まる
    //
    // 最初のフレームは phase の初期値の Combat からここへ来る
    // （本来の BT は L02_Update なので、まだ1度も走っていない）
    //
    // 手加減の BT が未設定なら入らない
    // （TutorialAttack で BT が空になり、ボスが止まったままになるのを防ぐ）
    if (inTutorial && !phase.tutorialAssetPath.empty())
        return bossWaiting ? BossPhase::TutorialWait : BossPhase::TutorialAttack;

    return phase.phase;
}



void BossPhaseSystem::OnEnterPhase(BossPhase                next,
                                   BossPhaseComponent      &phase,
                                   BehaviorTreeComponent   &bt,
                                   BossCommandComponent    &cmd,
                                   AnimParametersComponent &anim,
                                   HealthComponent         &health) const
{
    switch (next)
    {
    case BossPhase::Transition:
        // BT を完全に止める
        // assetPath を空にすると BehaviorTreeSystem が何もせずに戻り、
        // BossCommandComponent が上書きされないので、下の指示がそのまま効く
        // sharedAsset は触らない
        // （書くのは SyncAssets だけで、同じフレームのうちに null にする）
        bt.assetPath.clear();

        // 攻撃を止めてドローンを頭上へ退避させる
        // None は DroneCommandSystem が「命令なし」として無視するので使わない
        cmd.droneAttackActive = false;
        cmd.requestFormation  = DroneFormationType::HighOrbit;

        // 演出中は無敵にする（TickPhase でも毎フレーム保つ）
        health.invincibilityTimer = phase.transitionDuration;

        anim.SetTrigger(CCL::Utils::HashString(kRoarTrigger));
        phase.timer = phase.transitionDuration;

        // とどめのスローを見せてから、移行ムービーを頼む（頼むのは Update の 6）
        // ムービーはカメラを奪うので、すぐ頼むとスローが見えない
        Time::SlowWorld(*m_world, phase.finishSlowScale, phase.finishSlowDuration);
        phase.movieDelayTimer = phase.finishSlowDuration;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] 第2形態へ移行開始（%.1f秒）", phase.transitionDuration);
        break;

    case BossPhase::Combat2:
        // assetPath が変わると BehaviorTreeSystem が自動ロードする
        bt.assetPath              = phase.phase2AssetPath;
        health.invincibilityTimer = 0.0f; // 無敵解除
        phase.timer               = 0.0f;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] 第2形態 開始 → %s", bt.assetPath.c_str());
        break;

    case BossPhase::Dead:
        // 死亡アニメはここで1回だけトリガーを引く
        // 毎フレーム評価される BT では「1回だけ」を書きにくい
        // （この System がある理由）
        // 入ったときにも指示を消すのは、
        // 死んだ瞬間に攻撃の指示が1フレーム残らないようにするため
        anim.SetTrigger(CCL::Utils::HashString(kDeathTrigger));

        cmd.droneAttackActive = false;
        cmd.requestFormation  = DroneFormationType::None;

        phase.timer = 0.0f;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] ボス死亡 → Dead へ");
        break;

    case BossPhase::TutorialWait:
        // BT を止め、攻撃を止めてドローンを頭上で回す（Transition と同じ止め方）
        // TutorialAttack から来たときは撃ちかけの指示が残っているので、
        // それもここで消す
        bt.assetPath.clear();
        cmd.droneAttackActive = false;
        cmd.requestFormation  = DroneFormationType::HighOrbit;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] チュートリアル 待機");
        break;

    case BossPhase::TutorialAttack:
        // 手加減の BT へ差し替える
        // assetPath が変わると BehaviorTreeSystem が読み直す
        // 本来の BT は覚えておかない。チュートリアルシーンでは本来の戦いへ戻らないため
        bt.assetPath = phase.tutorialAssetPath;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] チュートリアル 手加減 → %s", bt.assetPath.c_str());
        break;

    case BossPhase::Combat:
        // phase の初期値なので「入る」ことはない（Combat へ移る遷移がない）
        break;
    }
}



void BossPhaseSystem::TickPhase(const BossPhaseComponent &phase,
                                BossCommandComponent     &cmd,
                                HealthComponent          &health) const
{
    switch (phase.phase)
    {
    case BossPhase::Transition:
        // 演出中はドローンの攻撃を止めたまま、無敵を保つ
        // （本体は BT が止まっているので要求が書かれない）
        cmd.droneAttackActive = false;
        if (health.invincibilityTimer < phase.timer)
            health.invincibilityTimer = phase.timer;
        break;

    case BossPhase::Dead:
        // 死んでいるのに攻撃が飛ぶのを防ぐ
        cmd.droneAttackActive = false;
        cmd.requestFormation  = DroneFormationType::None;
        break;

    case BossPhase::TutorialWait:
        // 待っている間は攻撃を出さない
        // BT は止めてあるので上書きされないが、念のため毎フレーム消す
        cmd.droneAttackActive = false;
        break;

    case BossPhase::Combat:
    case BossPhase::Combat2:
    case BossPhase::TutorialAttack:
        // 技は BT が選ぶ
        break;
    }
}



// とどめ以外の攻撃で、HP をどこより下げないかを決める
// 第1形態は第2形態へ移る線のすぐ上、最後の形態は 1。それ以外は 0（下限なし）
float BossPhaseSystem::DecideFinisherFloor(const BossPhaseComponent &phase,
                                           const HealthComponent    &health) const
{
    switch (phase.phase)
    {
    case BossPhase::Combat:
        // 第2形態がないボスは、第1形態が最後の形態になる
        // （DecideNextPhase と同じ条件で、移行するかを見る）
        if (phase.phase2AssetPath.empty() || health.maxHealth <= 0.0f) return kLastStandHealth;
        return health.maxHealth * (phase.phase2HealthThreshold + kLineMarginPercent) / 100.0f;

    case BossPhase::Combat2:
        return kLastStandHealth;

    case BossPhase::Transition:
    case BossPhase::Dead:
    case BossPhase::TutorialWait:
    case BossPhase::TutorialAttack:
        // 移行演出は無敵、死亡後は守るものがない
        // チュートリアルは TutorialHealthGuardSystem が HP を守る
        return 0.0f;
    }
    return 0.0f;
}



// BehaviorTreeSystem(L02_Update) が assetPath を読む前に形態を決める
// 第２形態に移行する際のassetPathを書き換えるため
REGISTER_LOGIC_SYSTEM(BossPhaseSystem, Priority::LogicStage::L02_PreUpdate)
