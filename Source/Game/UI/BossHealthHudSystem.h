#pragma once
// ボスHPゲージHUDを画面上部に構築し、ボスの HealthComponent に毎フレーム連動させる。
//
// 設計方針:
//   - PlayerHealthHudSystem と同じ「Initialize で UI 生成 → Update でデータ連動」の型。
//     UIProgressBar の「遅延ダメージ（チップ）バー」をそのまま流用する（表現＝ウィジェット、
//     供給＝本System の分離。ウィジェットの再利用性の実証）。
//   - 対象は HealthComponent かつ BossActionComponent を持つ「生存中(HP>0)」のボス。
//     ボスが未出現／撃破済みのときはHUDを隠す（＝ボス戦中だけ画面上部に出る）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"

class BossHealthHudSystem : public CCL::ECS::IfSystem<BossHealthHudSystem,
                                CCL::ECS::Read<HealthComponent>,
                                CCL::ECS::Read<BossActionComponent>>
{
  public:
    BossHealthHudSystem()
        : IfSystem("BossHealthHudSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;

  private:
    CCL::ECS::EntityID m_barID   = CCL::ECS::InvalidEntityID; // ゲージ本体（UIProgressBar）
    CCL::ECS::EntityID m_labelID = CCL::ECS::InvalidEntityID; // ラベル（UIText）

    float m_prevRatio = 1.0f;  // 前フレームのHP比（被弾検出用）
    float m_shake     = 0.0f;  // 被弾の揺れ残量 0..1（毎フレーム減衰させる）
    float m_shakeTime = 0.0f;  // 揺れの位相[秒]。世界の時計(dt)で進めるのでポーズ中は止まる。
    float m_trailHold = 0.0f;  // 被弾後、遅延バーが減り始めるまでの溜め(秒)
    bool  m_init      = false; // 初回にゲージ値をスナップ済みか

    // --- チューニング用 ---
    static constexpr float kTrailSpeed = 5.0f;  // 遅延バーの追従速度（大きいほど速く追いつく）
    static constexpr float kTrailHold  = 0.35f; // 被弾後、遅延バーが減り始めるまでの溜め(秒)
    static constexpr float kShakeDecay = 9.0f;  // 揺れの減衰速度（大きいほど短くビクッと終わる）
};
