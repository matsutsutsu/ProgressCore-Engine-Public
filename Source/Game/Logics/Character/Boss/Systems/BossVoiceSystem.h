#pragma once
// ===========================================================================
// 概要: 戦いの大事な場面で、ボスにセリフをしゃべらせる System
//
// 責務:
//   - 戦闘開始・第2形態への移行・撃破・プレイヤーを倒した、の4つの場面で1回ずつしゃべらせる
//   - 怒りに入ったときと、疲れたときに、そのたびに声を出させる
//   - 場面ごとの候補から1つをランダムに選び、2D で鳴らす
//
// 備考: どの場面も同時には起きないので、セリフどうしの重なりは気にしない
//       距離で小さくならないよう 2D で鳴らす
//       セリフの一覧と訳は Docs/Game/BossVoiceList.md
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"

#include <random>
#include <vector>

class BossVoiceSystem : public CCL::ECS::IfSystem<
    BossVoiceSystem,
    CCL::ECS::Read<BossPhaseComponent>,
    CCL::ECS::Read<HealthComponent>,
    CCL::ECS::Read<BossCommandComponent>,
    CCL::ECS::Read<BossActionComponent>>
{
    using Base = CCL::ECS::IfSystem<
        BossVoiceSystem,
        CCL::ECS::Read<BossPhaseComponent>,
        CCL::ECS::Read<HealthComponent>,
        CCL::ECS::Read<BossCommandComponent>,
        CCL::ECS::Read<BossActionComponent>>;

public:
    BossVoiceSystem() : IfSystem("BossVoiceSystem") {}

    void Update(float dt) override;
    void OnGui() override;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

private:
    // 候補から1つをランダムに選んで鳴らす
    void PlayRandom(const std::vector<const char *> &lines);

    // プレイヤーが倒れているか（プレイヤーがいなければ false）
    bool IsPlayerDown() const;

    // 前のフレームの状態（変わった瞬間を拾う）
    bool            m_hasPrev    = false;
    BossPhase       m_prevPhase  = BossPhase::Combat;
    BossActionState m_prevAction = BossActionState::None;
    bool            m_prevRaging = false;

    float m_combatTime     = 0.0f; // 第1形態がムービーなしで続いた時間 [秒]
    bool  m_saidBattle     = false;
    bool  m_saidPlayerDown = false;

    std::mt19937 m_random{std::random_device{}()};
};
