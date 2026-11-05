#pragma once
// ===========================================================================
// 概要: ボスの形態を決める System
//       （チュートリアルの待機・手加減 / 第1形態 / 移行演出 / 第2形態 / 死亡）
//
// 責務:
//   - 毎フレームの最初に、ボス本体への要求(currentActionId)を空にする
//   - HP と経過時間から、次の形態を決める
//   - 形態に入った瞬間だけの処理（BT の差し替え・無敵・アニメのトリガー・移行ムービーの再生要求）を行う
//   - 移行演出中と死亡中は、ドローンへの指示を止め続ける
//
// 備考: ボスの System の並び（すべて L02）
//         BossPerceptionSystem … 判断材料を書く（PreUpdate）
//         BossPhaseSystem      … 形態を決める（PreUpdate、Perception の後）
//         BehaviorTreeSystem   … 技を選ぶ（Update）
//         BossActionSystem     … 体の動きに変える（PostUpdate）
//       BT の root が優先度順の状態選択なので、ここは「入った瞬間に1回だけ」の処理だけを持つ
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <vector>
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
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

    // どちらも L02_PreUpdate で BehaviorTreeComponent を書くので、触る欄が別でも順序を決めておく
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BossPerceptionSystem"};
    }

    void Update(float dt) override;

private:
    // このフレームの遷移先を決める。優先度順に調べ、最初に成立したものを返す
    // inTutorial なら最後まで TutorialWait / TutorialAttack で戦い、bossWaiting で2つを切り替える
    // cinematic（ムービーが流れている）の間は、移行演出を終えない
    BossPhase DecideNextPhase(const BossPhaseComponent &phase,
                              const HealthComponent    &health,
                              bool                      inTutorial,
                              bool                      bossWaiting,
                              bool                      cinematic) const;

    // 形態に入った瞬間だけ走る処理（BT では書きにくい部分）
    // BT の差し替え・無敵・アニメのトリガー・ドローンへの指示を行う
    void OnEnterPhase(BossPhase                next,
                      BossPhaseComponent      &phase,
                      BehaviorTreeComponent   &bt,
                      BossCommandComponent    &cmd,
                      AnimParametersComponent &anim,
                      HealthComponent         &health) const;

    // 形態に留まっている間、毎フレーム走る処理
    // 移行演出中・死亡中・チュートリアルの待機中に、ドローンの攻撃を止め続ける
    void TickPhase(const BossPhaseComponent &phase,
                   BossCommandComponent     &cmd,
                   HealthComponent          &health) const;

    // Valkyra のアニメグラフの任意ステートから Death → DeathLoop へ入る
    // DeathLoop には出る遷移がないので、撃破ムービーとリザルトの間も倒れた姿のまま残る
    static constexpr const char *kDeathTrigger = "Death";

    // Valkyra のアニメグラフに Roar の遷移はまだなく、今は引いても何も起きない
    // グラフに足せばそのまま繋がる。アニメがなくても形態の移行はできる
    static constexpr const char *kRoarTrigger = "Roar";

    // 形態移行の演出時間（秒）。この間ボスは無敵で、BT は止まる
    // ムービーが流れている間は、この時間を過ぎても移行演出を続ける
    static constexpr float kTransitionDuration = 2.5f;

    // 移行演出に入った瞬間に流すムービーの台本
    static constexpr const char *kPhase2Cinematic = "Assets/Cinematics/BossPhase2.json";
};
