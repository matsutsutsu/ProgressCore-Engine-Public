#pragma once
// ===========================================================================
// 概要: チュートリアル中、プレイヤーとボスの HP を下限で止める System。
//
// 責務:
//   - プレイヤーの HP を kPlayerMinHealth より下げない（倒れない）
//   - ボスの HP を、第2形態へ移るしきい値より下げない（形態移行が始まらない）
//
// 備考: チュートリアルは勝ち負けを決める場ではないので、倒れて練習が途切れないようにする。
//       ボスも同じで、章2で攻撃を当てる練習をしているうちに HP が削れ、形態移行の演出（Transition）が
//       始まってしまわないようにする。どちらも無敵にはしない。被弾ののけぞりと HP の減りは本番と同じに見せる。
//       ダメージで HP を引く DamageResolutionSystem（L06_DamageApply）と、0 以下で DeadTag を付ける
//       HealthSystem（L06_HealthManagement）の間の段で走る。ここで戻しておけば、下限より下の HP を見る System は無い。
//       全体登録はせず、TutorialScene だけが登録する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"

class TutorialHealthGuardSystem : public CCL::ECS::IfSystem<TutorialHealthGuardSystem, CCL::ECS::Write<HealthComponent>>
{
public:
    TutorialHealthGuardSystem() : IfSystem("TutorialHealthGuardSystem") {}

    void Update(float dt) override;

    // プレイヤーとボスを ForEach の引数に取らずに見分けるので、ここで申告する
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

private:
    static constexpr float kPlayerMinHealth = 1.0f; // プレイヤーの HP の下限。0 より上なら倒れない

    // ボスの HP の下限を、しきい値よりどれだけ上に置くか（最大 HP に対する割合、%）。
    // しきい値ちょうどだと「しきい値以下」の判定に入ってしまうので、少し上に置く
    static constexpr float kBossMarginPercent = 1.0f;
};
