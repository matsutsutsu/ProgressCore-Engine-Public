#pragma once
// ===========================================================================
// 概要: 画面左上にプレイヤーの HP ゲージを作り、HealthComponent に毎フレーム合わせる System。
//
// 責務:
//   - Initialize でゲージとラベルの UI エンティティを作る
//   - HP の割合と色（残りに応じて緑・黄・赤）、HP の数値を毎フレーム書く
//   - 被弾したら、失った分（value〜trailValue）を少し見せてから遅れて減らし、ゲージを揺らす
//
// 備考: 対象は HealthComponent と PlayerComponent を持つエンティティ。居ないシーンでは隠す。
// ===========================================================================
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
    CCL::ECS::EntityID m_barID   = CCL::ECS::InvalidEntityID; // ゲージ本体（UIProgressBar）
    CCL::ECS::EntityID m_labelID = CCL::ECS::InvalidEntityID; // ラベル（UIText）

    float m_prevRatio = 1.0f;  // 前のフレームの HP の割合（被弾したかを見るため）
    float m_shake     = 0.0f;  // 被弾の揺れの残り（0〜1。毎フレーム減らす）
    float m_shakeTime = 0.0f;  // 揺れの経過時間（秒）。ゲームの dt で進めるのでポーズ中は止まる
    float m_trailHold = 0.0f;  // 被弾してから遅れて減るバーが動き出すまでの残り時間（秒）
    bool  m_init      = false; // ゲージの値を最初に合わせたか

    // --- 調整値 ---
    static constexpr float kTrailSpeed = 6.0f;  // 遅れて減るバーの追いつく速さ（大きいほど速い）
    static constexpr float kTrailHold  = 0.30f; // 被弾してから遅れて減るバーが動き出すまでの時間（秒）
    static constexpr float kShakeDecay = 9.0f;  // 揺れが収まる速さ（大きいほど短い）
};
