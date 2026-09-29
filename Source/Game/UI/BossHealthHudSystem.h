#pragma once
// ===========================================================================
// 概要: 画面上部にボスの HP ゲージを作り、ボスの HealthComponent に毎フレーム合わせる System。
//
// 責務:
//   - Initialize でゲージとラベルの UI エンティティを作る
//   - HP の割合・被弾したときの揺れ・遅れて減るバー（失った分の表示）を毎フレーム書く
//   - ボスが居ないとき・倒したときはゲージを隠す
//
// 備考: 対象は HealthComponent と BossActionComponent を持ち、HP が 0 より大きいボス。
//       ゲージの見た目は UIProgressBar が持ち、この System は値を渡すだけ（PlayerHealthHudSystem と同じ形）。
// ===========================================================================
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

    float m_prevRatio = 1.0f;  // 前のフレームの HP の割合（被弾したかを見るため）
    float m_shake     = 0.0f;  // 被弾の揺れの残り（0〜1。毎フレーム減らす）
    float m_shakeTime = 0.0f;  // 揺れの経過時間（秒）。ゲームの dt で進めるのでポーズ中は止まる
    float m_trailHold = 0.0f;  // 被弾してから遅れて減るバーが動き出すまでの残り時間（秒）
    bool  m_init      = false; // ゲージの値を最初に合わせたか

    // --- 調整値 ---
    static constexpr float kTrailSpeed = 5.0f;  // 遅れて減るバーの追いつく速さ（大きいほど速い）
    static constexpr float kTrailHold  = 0.35f; // 被弾してから遅れて減るバーが動き出すまでの時間（秒）
    static constexpr float kShakeDecay = 9.0f;  // 揺れが収まる速さ（大きいほど短い）
};
