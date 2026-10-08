#pragma once
// ===========================================================================
// 概要: ゲームの出来事（ボス撃破など）を見て、対応するムービーの再生を要求する System。
//
// 責務:
//   - ボスの形態が Dead に入った瞬間に、きっかけ名 "BossDefeated" の再生要求を出す
//   - ボスごとに前回の形態を覚え、形態に入った瞬間を1回だけ拾う
//
// 備考:
//   なぜボスの処理（BossPhaseSystem）に直接書かないか:
//   形態を決める処理は、ムービーがあるかどうかを知らなくてよい。
//   「何が起きたらどのムービーを流すか」の対応をここにまとめておけば、
//   ムービーを差し替える・増やすときにボスのコードへ手を入れずに済む。
//
//   どのムービーが流れるかは、TimelinePlayer の trigger と、ここで出す名前の一致で決まる。
//   流したいムービーのプレハブに trigger を書けばよく、その Entity ID を知る必要は無い。
//
//   ムービーを置いていないシーン（EditorScene など）でも要求は出る。
//   受け手が居なければ TimelineSystem がログを出すだけで、何も起きない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Timeline/Components/TimelinePlayerComponent.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"

#include <unordered_map>
#include <vector>

class CinematicTriggerSystem : public CCL::ECS::IfSystem<CinematicTriggerSystem,
                                                         CCL::ECS::Read<BossPhaseComponent>>
{
public:
    CinematicTriggerSystem()
        : IfSystem("CinematicTriggerSystem")
    {
    }
    virtual ~CinematicTriggerSystem() = default;

    void Update(float dt) override;

    /**
     * @brief 書き込む Component 型をスケジューラへ申告する。
     *
     * 再生要求を受けた TimelineSystem の購読は、この System のスレッドで同期に走り、
     * TimelinePlayerComponent を書き換える。ForEach のフィルタ（BossPhaseComponent）には
     * 載らないので、ここで足しておかないと同じステージの System と並列に走ってしまう。
     */
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = IfSystem::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<TimelinePlayerComponent>::ID());
        return types;
    }

private:
    // ボスごとの前回の形態。形態に「入った瞬間」を拾うために持つ
    std::unordered_map<CCL::ECS::EntityID, BossPhase> m_prevPhase;

    static constexpr const char *kBossDefeatedTrigger = "BossDefeated"; // ボス撃破ムービーのきっかけ名
};
