#pragma once
// ===========================================================================
// 概要: 戦いの大事な場面で、ボスにセリフをしゃべらせる System
//
// 責務:
//   - 戦闘開始・第2形態への移行・撃破・プレイヤーを倒した、の4つの場面で1回ずつしゃべらせる
//   - 場面ごとの候補から1つをランダムに選び、2D で鳴らす
//
// 備考: どの場面も1回の戦いで1回しか起きず、同時にも起きないので、セリフどうしの重なりは気にしない
//       距離で小さくならないよう 2D で鳴らす
//       セリフの一覧と訳は Docs/Game/BossVoiceList.md
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"

#include <random>
#include <vector>

class BossVoiceSystem : public CCL::ECS::IfSystem<
    BossVoiceSystem,
    CCL::ECS::Read<BossPhaseComponent>,
    CCL::ECS::Read<HealthComponent>>
{
    using Base = CCL::ECS::IfSystem<
        BossVoiceSystem,
        CCL::ECS::Read<BossPhaseComponent>,
        CCL::ECS::Read<HealthComponent>>;

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

    bool      m_hasPrev   = false;
    BossPhase m_prevPhase = BossPhase::Combat; // 前のフレームの形態（変わった瞬間を拾う）

    float m_combatTime     = 0.0f; // 第1形態がムービーなしで続いた時間 [秒]
    bool  m_saidBattle     = false;
    bool  m_saidPlayerDown = false;

    std::mt19937 m_random{std::random_device{}()};
};
