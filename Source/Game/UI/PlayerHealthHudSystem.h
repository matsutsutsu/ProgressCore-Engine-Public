#pragma once
/// @file PlayerHealthHudSystem.h
/// @brief プレイヤーHPゲージHUDを ECS UI で構築し、HealthComponent に毎フレーム連動させる。
///
/// 設計方針:
///   - CleaveGaugeHudSystem と同じ「Initialize で UI エンティティ生成 → Update でデータ連動」の型。
///   - 本体フィルは現在HPへ即追従。被弾時は UIProgressBar の「遅延ダメージ（チップ）バー」を使い、
///     失った分(value..trailValue)を一瞬見せてからスッと減らす（格ゲー風のHPバー演出）。
///   - プレイヤー(HealthComponent かつ PlayerComponent)だけを対象にする。居ないシーンでは隠す。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

class PlayerHealthHudSystem : public CCL::ECS::IfSystem<PlayerHealthHudSystem,
                                  CCL::ECS::Read<HealthComponent>,
                                  CCL::ECS::Read<PlayerComponent>>
{
  public:
    PlayerHealthHudSystem()
        : IfSystem("PlayerHealthHudSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;

  private:
    CCL::ECS::EntityID m_barID   = CCL::ECS::InvalidEntityID; ///< ゲージ本体（UIProgressBar）
    CCL::ECS::EntityID m_labelID = CCL::ECS::InvalidEntityID; ///< ラベル（UIText）

    float m_prevRatio = 1.0f;   ///< 前フレームのHP比（被弾検出用）
    float m_trailHold = 0.0f;   ///< 被弾後、遅延バーが減り始めるまでの溜め(秒)
    bool  m_init      = false;  ///< 初回にゲージ値をスナップ済みか

    // --- チューニング用 ---
    static constexpr float kTrailSpeed = 6.0f;  ///< 遅延バーの追従速度（大きいほど速く追いつく）
    static constexpr float kTrailHold  = 0.30f; ///< 被弾後、遅延バーが減り始めるまでの溜め(秒)
};
