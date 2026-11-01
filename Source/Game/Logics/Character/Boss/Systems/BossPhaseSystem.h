#pragma once
// ===========================================================================
// 概要: ボスの形態を決める System。
//       （チュートリアルの待機・手加減 / 第1形態 / 移行演出 / 第2形態 / 死亡）
//
// 責務:
//   - 毎フレームの最初に、ボス本体への要求(currentActionId)を空にする
//   - HP と経過時間から、次の形態を決める
//   - 形態に入った瞬間だけの処理（BT の差し替え・無敵・アニメのトリガー・移行ムービーの再生要求）を行う
//   - 移行演出中と死亡中は、ドローンへの指示を止め続ける
//
// 備考:
//   ボスの System の並び:
//     BossPerceptionSystem … blackboard へ判断材料を書く  L02_PreUpdate
//     BossPhaseSystem      … 形態を決める                  L02_PreUpdate（Perception の後）← このファイル
//     BehaviorTreeSystem   … 技を選ぶ                      L02_Update
//     BossActionSystem     … 体の動きに変える              L02_PostUpdate
//
//   汎用のステートマシンを使わないのは、
//   BT の root が既に優先度順の状態選択になっているから。
//   BT で書けない「入った瞬間に1回だけ」の処理のためだけに、
//   この小さな FSM を置いている。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <vector>
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BehaviorTreeComponent
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Time/Components/TimeState.h"

class BossPhaseSystem : public CCL::ECS::IfSystem<
                            BossPhaseSystem,
                            CCL::ECS::Write<BossPhaseComponent>,
                            CCL::ECS::Write<BehaviorTreeComponent>,
                            CCL::ECS::Write<BossCommandComponent>,
                            CCL::ECS::Write<AnimParametersComponent>,
                            CCL::ECS::Write<HealthComponent>,
                            CCL::ECS::Read<TimeStateComponent>>
{
public:
    BossPhaseSystem() : IfSystem("BossPhaseSystem") {}
    ~BossPhaseSystem() override = default;

    /**
     * @brief 同じステージで先に走らせる System を宣言する。
     *
     * BossPerceptionSystem の後に走らせる。
     * どちらも L02_PreUpdate で BehaviorTreeComponent を Write するので、
     * 触るフィールドが重ならなくても、
     * スケジューラには順序が決まっていないと報告される。
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BossPerceptionSystem"};
    }

    void Update(float dt) override;

private:
    /**
     * @brief このフレームの遷移先を決める。
     *
     * 優先度順に調べ、最初に成立したものを返す。
     *
     * @param phase       今の形態と設定値。
     * @param health      ボスの HP。
     * @param inTutorial  チュートリアルシーンか。
     *                    true なら最初から最後まで TutorialWait / TutorialAttack で戦う。
     * @param bossWaiting ボスを待たせるか（攻撃を覚える前の項目と、教え終えた後）。
     *                    true なら TutorialWait、false なら TutorialAttack にする。
     * @param cinematic   ムービーが流れているか。流れている間は移行演出を終えない。
     * @return 遷移先の形態。変わらなければ今の形態。
     */
    BossPhase DecideNextPhase(const BossPhaseComponent &phase,
                              const HealthComponent    &health,
                              bool                      inTutorial,
                              bool                      bossWaiting,
                              bool                      cinematic) const;

    // 形態に入った瞬間だけ走る処理（BT では書きにくい部分）。
    // BT の差し替え・無敵・アニメのトリガー・ドローンへの指示を行う。
    void OnEnterPhase(BossPhase                next,
                      BossPhaseComponent      &phase,
                      BehaviorTreeComponent   &bt,
                      BossCommandComponent    &cmd,
                      AnimParametersComponent &anim,
                      HealthComponent         &health) const;

    // 形態に留まっている間、毎フレーム走る処理。
    // 移行演出中・死亡中・チュートリアルの待機中に、ドローンの攻撃を止め続ける。
    void TickPhase(const BossPhaseComponent &phase,
                   BossCommandComponent     &cmd,
                   HealthComponent          &health) const;

    // Death は Valkyra のアニメグラフの任意ステートからの遷移で、
    // よろめいて動けなくなるアニメ（Death → DeathLoop）に入る。
    // DeathLoop には出る遷移が無いので、
    // 撃破ムービーとリザルトの間も倒れた姿のまま残る。
    static constexpr const char *kDeathTrigger = "Death";

    // Roar は Valkyra のアニメグラフに対応する遷移が無いので、
    // 今はトリガーを引いても何も起きない（グラフに足せばそのまま繋がる）。
    // アニメが無くても形態の移行はできる。
    static constexpr const char *kRoarTrigger = "Roar";

    // 形態移行の演出時間（秒）。この間ボスは無敵で、BT は止まる。
    // 移行ムービーが流れている間は、この時間を過ぎても移行演出を続ける（ムービーの方が長くてよい）。
    // ムービーの台本が無いときは、この時間で第2形態へ移る。
    static constexpr float kTransitionDuration = 2.5f;

    // 移行演出に入った瞬間に流すムービーの台本
    static constexpr const char *kPhase2Cinematic = "Assets/Cinematics/BossPhase2.json";
};
